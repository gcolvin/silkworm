/*
   Copyright 2020 The Silkworm Authors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "processor.hpp"

#include <algorithm>
#include <ethash/keccak.hpp>
#include <intx/int128.hpp>
#include <iostream>
#include <silkworm/chain/dao.hpp>
#include <utility>

#include "execution.hpp"
#include "protocol_param.hpp"

namespace silkworm {

intx::uint128 intrinsic_gas(const Transaction& txn, bool homestead, bool istanbul) {
    intx::uint128 gas{fee::kGTransaction};
    if (!txn.to && homestead) {
        gas += fee::kGTxCreate;
    }

    if (txn.data.empty()) {
        return gas;
    }

    intx::uint128 non_zero_bytes{std::count_if(txn.data.begin(), txn.data.end(), [](char c) { return c != 0; })};

    uint64_t nonZeroGas{istanbul ? fee::kGTxDataNonZeroIstanbul : fee::kGTxDataNonZeroFrontier};
    gas += non_zero_bytes * nonZeroGas;

    intx::uint128 zero_bytes{txn.data.length() - non_zero_bytes};
    gas += zero_bytes * fee::kGTxDataZero;

    return gas;
}

ExecutionProcessor::ExecutionProcessor(const Block& block, IntraBlockState& state, const ChainConfig& config)
    : evm_{block, state, config} {}

/*
static void print_gas_used(const Transaction& txn, uint64_t gas_used) {
    Bytes rlp{};
    rlp::encode(rlp, txn);
    ethash::hash256 hash{keccak256(rlp)};
    std::cout << "0x" << to_hex(full_view(hash.bytes)) << " " << gas_used << "\n";
}
*/

Receipt ExecutionProcessor::execute_transaction(const Transaction& txn) {
    IntraBlockState& state{evm_.state()};

    if (!txn.from) {
        throw ValidationError("missing sender");
    }

    uint64_t nonce{state.get_nonce(*txn.from)};
    if (nonce != txn.nonce) {
        throw ValidationError("invalid nonce");
    }

    uint64_t block_number{evm_.block().header.number};
    bool homestead{evm_.config().has_homestead(block_number)};
    bool spurious_dragon{evm_.config().has_spurious_dragon(block_number)};
    bool istanbul{evm_.config().has_istanbul(block_number)};

    intx::uint128 g0{intrinsic_gas(txn, homestead, istanbul)};
    if (txn.gas_limit < g0) {
        throw ValidationError("intrinsic gas");
    }

    intx::uint512 gas_cost{intx::umul(intx::uint256{txn.gas_limit}, txn.gas_price)};
    intx::uint512 v0{gas_cost + txn.value};

    if (state.get_balance(*txn.from) < v0) {
        throw ValidationError("insufficient funds");
    }

    if (available_gas() < txn.gas_limit) {
        throw ValidationError("block gas limit reached");
    }

    state.subtract_from_balance(*txn.from, gas_cost.lo);
    if (txn.to) {
        // EVM itself increments the nonce for contract creation
        state.set_nonce(*txn.from, nonce + 1);
    }

    evm_.state().clear_journal_and_substate();

    CallResult vm_res{evm_.execute(txn, txn.gas_limit - g0.lo)};

    uint64_t gas_used{txn.gas_limit - refund_gas(txn, vm_res.gas_left)};

    // award the miner
    state.add_to_balance(evm_.block().header.beneficiary, gas_used * txn.gas_price);

    evm_.state().destruct_suicides();
    if (spurious_dragon) {
        evm_.state().destruct_touched_dead();
    }

    evm_.state().finalize_transaction();

    cumulative_gas_used_ += gas_used;

    return {
        vm_res.status == EVMC_SUCCESS,    // success
        cumulative_gas_used_,             // cumulative_gas_used
        logs_bloom(evm_.state().logs()),  // bloom
        evm_.state().logs(),              // logs
    };
}

uint64_t ExecutionProcessor::available_gas() const { return evm_.block().header.gas_limit - cumulative_gas_used_; }

uint64_t ExecutionProcessor::refund_gas(const Transaction& txn, uint64_t gas_left) {
    uint64_t refund{std::min((txn.gas_limit - gas_left) / 2, evm_.state().total_refund())};
    gas_left += refund;
    evm_.state().add_to_balance(*txn.from, gas_left * txn.gas_price);
    return gas_left;
}

std::vector<Receipt> ExecutionProcessor::execute_block() {
    std::vector<Receipt> receipts{};

    uint64_t block_num{evm_.block().header.number};
    if (block_num == evm_.config().dao_block) {
        dao::transfer_balances(evm_.state());
    }

    cumulative_gas_used_ = 0;
    for (const Transaction& txn : evm_.block().transactions) {
        receipts.push_back(execute_transaction(txn));
    }

    apply_rewards();

    // See Yellow Paper, Appendix K "Anomalies on the Main Network"
    if (block_num == evm_.config().ripemd_deletion_block) {
        static constexpr evmc::address kRipemdAddress{0x0000000000000000000000000000000000000003_address};
        evm_.state().destruct(kRipemdAddress);
    }

    evm_.state().write_to_db(block_num);

    return receipts;
}

void ExecutionProcessor::apply_rewards() {
    uint64_t block_number{evm_.block().header.number};
    intx::uint256 block_reward;
    if (evm_.config().has_constantinople(block_number)) {
        block_reward = param::kConstantinopleBlockReward;
    } else if (evm_.config().has_byzantium(block_number)) {
        block_reward = param::kByzantiumBlockReward;
    } else {
        block_reward = param::kFrontierBlockReward;
    }

    intx::uint256 miner_reward{block_reward};
    for (const BlockHeader& ommer : evm_.block().ommers) {
        intx::uint256 ommer_reward{((8 + ommer.number - block_number) * block_reward) >> 3};
        evm_.state().add_to_balance(ommer.beneficiary, ommer_reward);
        miner_reward += block_reward / 32;
    }

    evm_.state().add_to_balance(evm_.block().header.beneficiary, miner_reward);
}
}  // namespace silkworm
