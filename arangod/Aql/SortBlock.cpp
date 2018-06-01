////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2016 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Max Neunhoeffer
////////////////////////////////////////////////////////////////////////////////

#include "SortBlock.h"
#include "Aql/AqlItemBlock.h"
#include "Aql/ExecutionEngine.h"
#include "Basics/Exceptions.h"
#include "VocBase/vocbase.h"

// TODO REMOVE ME, JUST FOR TEMP LOCKING
#include "Aql/Query.h"
using namespace arangodb::aql;

#ifdef USE_IRESEARCH

#include "IResearch/IResearchViewNode.h"
#include "IResearch/IResearchOrderFactory.h"
#include "IResearch/AqlHelper.h"

namespace {

int compareIResearchScores(
    irs::sort::prepared const* comparer,
    arangodb::transaction::Methods*,
    AqlValue const& lhs,
    AqlValue const& rhs
) {
  arangodb::velocypack::ValueLength tmp;

  auto const* lhsScore = reinterpret_cast<irs::byte_type const*>(lhs.slice().getString(tmp));
  auto const* rhsScore = reinterpret_cast<irs::byte_type const*>(rhs.slice().getString(tmp));

  if (comparer->less(lhsScore, rhsScore)) {
    return -1;
  }

  if (comparer->less(rhsScore, lhsScore)) {
    return 1;
  }

  return 0;
}

int compareAqlValues(
    irs::sort::prepared const*,
    arangodb::transaction::Methods* trx,
    AqlValue const& lhs,
    AqlValue const& rhs) {
  return AqlValue::Compare(
    reinterpret_cast<arangodb::transaction::Methods*>(trx), lhs, rhs, true
  );
}

void fillSortRegisters(
    std::vector<SortRegister>& sortRegisters,
    SortNode const& en
) {
  TRI_ASSERT(en.plan());
  auto& execPlan = *en.plan();

  auto const& elements = en.elements();
  sortRegisters.reserve(elements.size());
  std::unordered_map<ExecutionNode const*, size_t> offsets(sortRegisters.capacity());

  irs::sort::ptr comparer;

  for (auto const& p : elements) {
    auto const varId = p.var->id;
    auto it = en.getRegisterPlan()->varInfo.find(varId);
    TRI_ASSERT(it != en.getRegisterPlan()->varInfo.end());
    TRI_ASSERT(it->second.registerId < ExecutionNode::MaxRegisterId);
    sortRegisters.emplace_back(it->second.registerId, p.ascending, nullptr, &compareAqlValues);

    auto const* setter = execPlan.getVarSetBy(varId);

    if (setter && ExecutionNode::ENUMERATE_IRESEARCH_VIEW == setter->getType()) {
      // sort condition is covered by `IResearchViewNode`

#ifdef ARANGODB_ENABLE_MAINTAINER_MODE
      auto const& viewNode = dynamic_cast<arangodb::iresearch::IResearchViewNode const&>(*setter);
#else
      auto const& viewNode = static_cast<arangodb::iresearch::IResearchViewNode const&>(*setter);
#endif // ARANGODB_ENABLE_MAINTAINER_MODE

      auto& offset = offsets[&viewNode];
      auto* node = viewNode.sortCondition()[offset++].node;

      if (arangodb::iresearch::OrderFactory::comparer(&comparer, *node)) {
        auto& reg = sortRegisters.back();
        std::get<2>(reg) = comparer->prepare(); // set score comparer
        std::get<3>(reg) = &compareIResearchScores; // set comparator
      }
    }
  }
}

/// @brief OurLessThan
class OurLessThan {
 public:
  OurLessThan(
      arangodb::transaction::Methods* trx,
      std::deque<AqlItemBlock*>& buffer,
      std::vector<SortRegister>& sortRegisters) noexcept
    : _trx(trx),
      _buffer(buffer),
      _sortRegisters(sortRegisters) {
  }

  bool operator()(std::pair<uint32_t, uint32_t> const& a,
                  std::pair<uint32_t, uint32_t> const& b) const {
    for (auto const& reg : _sortRegisters) {
      auto const& lhs = _buffer[a.first]->getValueReference(a.second, std::get<0>(reg));
      auto const& rhs = _buffer[b.first]->getValueReference(b.second, std::get<0>(reg));

      auto const comparator = std::get<3>(reg);
      TRI_ASSERT(comparator);

      int const cmp = (*comparator)(std::get<2>(reg).get(), _trx, lhs, rhs);

      if (cmp < 0) {
        return std::get<1>(reg);
      } else if (cmp > 0) {
        return !std::get<1>(reg);
      }
    }

    return false;
  }

 private:
  arangodb::transaction::Methods* _trx;
  std::deque<AqlItemBlock*>& _buffer;
  std::vector<SortRegister>& _sortRegisters;
};

}

#else

void fillSortRegisters(
    std::vector<SortRegister>& sortRegisters,
    SortNode const& en
) {
  auto const& elements = en.elements();
  sortRegisters.reserve(elements.size());

  for (auto const& p : elements) {
    auto const varId = p.var->id;
    auto it = en.getRegisterPlan()->varInfo.find(varId);
    TRI_ASSERT(it != en.getRegisterPlan()->varInfo.end());
    TRI_ASSERT(it->second.registerId < ExecutionNode::MaxRegisterId);
    sortRegisters.emplace_back(it->second.registerId, p.ascending);
  }
}

/// @brief OurLessThan
class OurLessThan {
 public:
  OurLessThan(arangodb::transaction::Methods* trx,
              std::deque<AqlItemBlock*>& buffer,
              std::vector<SortRegister>& sortRegisters)
      : _trx(trx),
        _buffer(buffer),
        _sortRegisters(sortRegisters) {}

  bool operator()(std::pair<size_t, size_t> const& a,
                  std::pair<size_t, size_t> const& b) const {
    for (auto const& reg : _sortRegisters) {
      auto const& lhs = _buffer[a.first]->getValueReference(a.second, std::get<0>(reg));
      auto const& rhs = _buffer[b.first]->getValueReference(b.second, std::get<0>(reg));

      int const cmp = AqlValue::Compare(_trx, lhs, rhs, true);

      if (cmp < 0) {
        return std::get<1>(reg);
      } else if (cmp > 0) {
        return !std::get<1>(reg);
      }
    }

    return false;
  }

 private:
  arangodb::transaction::Methods* _trx;
  std::deque<AqlItemBlock*>& _buffer;
  std::vector<SortRegister>& _sortRegisters;
};

#endif // USE_IRESEARCH

SortBlock::SortBlock(ExecutionEngine* engine, SortNode const* en)
  : ExecutionBlock(engine, en),
    _stable(en->_stable),
    _mustFetchAll(true) {
  TRI_ASSERT(en);
  fillSortRegisters(_sortRegisters, *en);
}

SortBlock::~SortBlock() {}

std::pair<ExecutionState, arangodb::Result> SortBlock::initializeCursor(
    AqlItemBlock* items, size_t pos) {
  DEBUG_BEGIN_BLOCK(); 
  auto res = ExecutionBlock::initializeCursor(items, pos);

  if (res.first == ExecutionState::WAITING ||
      !res.second.ok()) {
    // If we need to wait or get an error we return as is.
    return res;
  }

  _mustFetchAll = !_done;
  _pos = 0;

  return res; 

  // cppcheck-suppress style
  DEBUG_END_BLOCK();  
}

int SortBlock::getOrSkipSomeOld(size_t atMost, bool skipping,
                               AqlItemBlock*& result, size_t& skipped) {
  DEBUG_BEGIN_BLOCK(); 
  
  TRI_ASSERT(result == nullptr && skipped == 0);
  
  if (_mustFetchAll) {
    // suck all blocks into _buffer
    while (getBlock(DefaultBatchSize())) {
    }

    _mustFetchAll = false;
    if (!_buffer.empty()) {
      doSorting();
    }
  }

  while (true) {
    auto res = ExecutionBlock::getOrSkipSome(atMost, skipping, result, skipped);
    if (res.first == ExecutionState::WAITING) {
      _engine->getQuery()->tempWaitForAsyncResponse();
    } else { 
      return res.second.errorNumber();
    }
  }
  // cppcheck-suppress style
  DEBUG_END_BLOCK();  
}

void SortBlock::doSorting() {
  DEBUG_BEGIN_BLOCK();  

  size_t sum = 0;
  for (auto const& block : _buffer) {
    sum += block->size();
  }

  TRI_IF_FAILURE("SortBlock::doSorting") {
    THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
  }
  
  // coords[i][j] is the <j>th row of the <i>th block
  std::vector<std::pair<uint32_t, uint32_t>> coords;
  coords.reserve(sum);

  // install the coords
  // we are intentionally using uint32_t here to save memory and
  // have better cache utilization
  uint32_t count = 0;

  for (auto const& block : _buffer) {
    uint32_t const n = static_cast<uint32_t>(block->size());
    
    for (uint32_t i = 0; i < n; i++) {
      coords.emplace_back(std::make_pair(count, i));
    }
    ++count;
  }

  // comparison function
  OurLessThan ourLessThan(_trx, _buffer, _sortRegisters);

  // sort coords
  if (_stable) {
    std::stable_sort(coords.begin(), coords.end(), ourLessThan);
  } else {
    std::sort(coords.begin(), coords.end(), ourLessThan);
  }

  // here we collect the new blocks (later swapped into _buffer):
  std::deque<AqlItemBlock*> newbuffer;

  try {  // If we throw from here, the catch will delete the new
    // blocks in newbuffer

    count = 0;
    RegisterId const nrRegs = _buffer.front()->getNrRegs();

    std::unordered_map<AqlValue, AqlValue> cache;

    // install the rearranged values from _buffer into newbuffer

    while (count < sum) {
      size_t sizeNext = (std::min)(sum - count, DefaultBatchSize());
      AqlItemBlock* next = requestBlock(sizeNext, nrRegs);

      try {
        TRI_IF_FAILURE("SortBlock::doSortingInner") {
          THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
        }
        newbuffer.emplace_back(next);
      } catch (...) {
        delete next;
        throw;
      }

      // only copy as much as needed!
      for (size_t i = 0; i < sizeNext; i++) {
        for (RegisterId j = 0; j < nrRegs; j++) {
          auto const& a =
              _buffer[coords[count].first]->getValueReference(coords[count].second, j);
          // If we have already dealt with this value for the next
          // block, then we just put the same value again:
          if (!a.isEmpty()) {
            if (a.requiresDestruction()) {
              // complex value, with ownership transfer
              auto it = cache.find(a);

              if (it != cache.end()) {
                // If one of the following throws, all is well, because
                // the new block already has either a copy or stolen
                // the AqlValue:
                _buffer[coords[count].first]->eraseValue(coords[count].second, j);
                next->setValue(i, j, (*it).second);
              } else {
                // We need to copy a, if it has already been stolen from
                // its original buffer, which we know by looking at the
                // valueCount there.
                auto vCount = _buffer[coords[count].first]->valueCount(a);

                if (vCount == 0) {
                  // Was already stolen for another block
                  AqlValue b = a.clone();
                  try {
                    TRI_IF_FAILURE("SortBlock::doSortingCache") {
                      THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
                    }
                    cache.emplace(a, b);
                  } catch (...) {
                    b.destroy();
                    throw;
                  }

                  try {
                    TRI_IF_FAILURE("SortBlock::doSortingNext1") {
                      THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
                    }
                    next->setValue(i, j, b);
                  } catch (...) {
                    cache.erase(b);
                    b.destroy();
                    throw;
                  }
                  // It does not matter whether the following works or not,
                  // since the original block keeps its responsibility
                  // for a:
                  _buffer[coords[count].first]->eraseValue(coords[count].second, j);
                } else {
                  TRI_IF_FAILURE("SortBlock::doSortingNext2") {
                    THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
                  }
                  // Here we are the first to want to inherit a, so we
                  // steal it:
                  next->setValue(i, j, a);
                  _buffer[coords[count].first]->steal(a);
                  _buffer[coords[count].first]->eraseValue(coords[count].second, j);
                  // If this has worked, responsibility is now with the
                  // new block or indeed with us!
                  // If the following does not work, we will create a
                  // few unnecessary copies, but this does not matter:
                  cache.emplace(a, a);
                }
              }
            } else {
              // simple value, which does not need ownership transfer
              TRI_IF_FAILURE("SortBlock::doSortingCache") {
                THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
              }
              TRI_IF_FAILURE("SortBlock::doSortingNext1") {
                THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
              }
              TRI_IF_FAILURE("SortBlock::doSortingNext2") {
                THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
              }
              next->setValue(i, j, a);
              _buffer[coords[count].first]->eraseValue(coords[count].second, j);
            }
          }
        }
        count++;
      }
      cache.clear();
    }
  } catch (...) {
    for (auto& x : newbuffer) {
      delete x;
    }
    throw;
  }
  _buffer.swap(newbuffer);  // does not throw since allocators
  // are the same
  for (auto& x : newbuffer) {
    delete x;
  }
  DEBUG_END_BLOCK();  
}
