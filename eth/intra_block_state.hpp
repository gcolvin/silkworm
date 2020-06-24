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

#ifndef SILKWORM_ETH_INTRA_BLOCK_STATE_H_
#define SILKWORM_ETH_INTRA_BLOCK_STATE_H_

#include <intx/intx.hpp>

#include "common.hpp"

namespace silkworm::eth {

class IntraBlockState {
 public:
  IntraBlockState(const IntraBlockState&) = delete;
  IntraBlockState& operator=(const IntraBlockState&) = delete;

  IntraBlockState() = default;

  bool Exists(AddressRef address) const;

  intx::uint256 GetBalance(AddressRef address) const;
  void AddBalance(AddressRef address, const intx::uint256& addend);
  void SubBalance(AddressRef address, const intx::uint256& subtrahend);

  uint64_t GetNonce(AddressRef address) const;
  void SetNonce(AddressRef address, uint64_t nonce);

  uint64_t GetRefund() const;
};

}  // namespace silkworm::eth

#endif  // SILKWORM_ETH_INTRA_BLOCK_STATE_H_