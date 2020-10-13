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

#ifndef SILKWORM_DB_BUFFER_H_
#define SILKWORM_DB_BUFFER_H_

#include <absl/container/flat_hash_set.h>

#include <evmc/evmc.hpp>
#include <map>
#include <optional>
#include <silkworm/db/chaindb.hpp>
#include <silkworm/db/change.hpp>
#include <silkworm/types/account.hpp>
#include <silkworm/types/block.hpp>

namespace silkworm::db {

class Buffer {
   public:
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    explicit Buffer(lmdb::Transaction* txn, std::optional<uint64_t> historical_block = std::nullopt)
        : txn_{txn}, historical_block_{historical_block} {}

    void insert_header(BlockHeader block_header);

    std::optional<BlockHeader> read_header(uint64_t block_number, const evmc::bytes32& block_hash) const noexcept;

    std::optional<Account> read_account(const evmc::address& address) const noexcept;

    Bytes read_code(const evmc::bytes32& code_hash) const noexcept;

    evmc::bytes32 read_storage(const evmc::address& address, uint64_t incarnation,
                               const evmc::bytes32& key) const noexcept;

    // Previous non-zero incarnation of an account; 0 if none exists
    uint64_t previous_incarnation(const evmc::address& address) const noexcept;

    void update_account(const evmc::address& address, std::optional<Account> initial, std::optional<Account> current);

    void update_account_code(const evmc::address& address, uint64_t incarnation, const evmc::bytes32& code_hash,
                             ByteView code);

    void update_storage(const evmc::address& address, uint64_t incarnation, const evmc::bytes32& key,
                        const evmc::bytes32& initial, const evmc::bytes32& current);

    void write_to_db(uint64_t block_number);

    const AccountChanges& account_back_changes() const { return account_back_changes_; }
    const StorageChanges& storage_back_changes() const { return storage_back_changes_; }

   private:
    lmdb::Transaction* txn_{nullptr};

    std::optional<uint64_t> historical_block_{};

    std::map<Bytes, BlockHeader> headers_{};

    AccountChanges account_back_changes_;
    StorageChanges storage_back_changes_;
    absl::flat_hash_set<evmc::address> changed_storage_;

    std::map<evmc::address, std::optional<Account>> accounts_;
    std::map<Bytes, std::map<evmc::bytes32, evmc::bytes32>> storage_;
    std::map<evmc::address, uint64_t> incarnations_;
    std::map<evmc::bytes32, Bytes> hash_to_code_;
    std::map<Bytes, evmc::bytes32> storage_prefix_to_code_hash_;
};

}  // namespace silkworm::db

#endif  // SILKWORM_DB_BUFFER_H_
