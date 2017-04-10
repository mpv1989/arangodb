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
/// @author Michael Hackstein
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGOD_VOC_BASE_TRAVERSER_OPTIONS_H
#define ARANGOD_VOC_BASE_TRAVERSER_OPTIONS_H 1

#include "Basics/Common.h"
#include "Basics/StringRef.h"
#include "Aql/FixedVarExpressionContext.h"
#include "Graph/BaseOptions.h"
#include "StorageEngine/TransactionState.h"
#include "Transaction/Methods.h"

namespace arangodb {
class ManagedDocumentResult;

namespace velocypack {
class Builder;
class Slice;
}

namespace aql {
struct AstNode;
class Expression;
class Query;
class TraversalNode;
}

namespace traverser {

class ClusterTraverser;
class TraverserCache;

/// @brief Abstract class used in the traversals
/// to abstract away access to indexes / DBServers.
/// Returns edges as VelocyPack.

class EdgeCursor {
 public:
  EdgeCursor() {}
  virtual ~EdgeCursor() {}

  virtual bool next(std::function<void(arangodb::StringRef const&, VPackSlice, size_t)> callback) = 0;

  virtual void readAll(std::function<void(arangodb::StringRef const&, arangodb::velocypack::Slice, size_t&)>) = 0;
};

struct TraverserOptions : public graph::BaseOptions {
  friend class arangodb::aql::TraversalNode;

 public:
  enum UniquenessLevel { NONE, PATH, GLOBAL };

 protected:
  std::unordered_map<uint64_t, std::vector<LookupInfo>> _depthLookupInfo;
  std::unordered_map<uint64_t, aql::Expression*> _vertexExpressions;
  aql::Expression* _baseVertexExpression;
  arangodb::traverser::ClusterTraverser* _traverser;

 public:
  uint64_t minDepth;

  uint64_t maxDepth;

  bool useBreadthFirst;

  UniquenessLevel uniqueVertices;

  UniquenessLevel uniqueEdges;

  explicit TraverserOptions(transaction::Methods* trx);

  TraverserOptions(transaction::Methods*, arangodb::velocypack::Slice const&);

  TraverserOptions(arangodb::aql::Query*, arangodb::velocypack::Slice,
                   arangodb::velocypack::Slice);

  /// @brief This copy constructor is only working during planning phase.
  ///        After planning this node should not be copied anywhere.
  TraverserOptions(TraverserOptions const&);

  virtual ~TraverserOptions();

  /// @brief Build a velocypack for cloning in the plan.
  void toVelocyPack(arangodb::velocypack::Builder&) const;
  
  /// @brief Build a velocypack for indexes
  void toVelocyPackIndexes(arangodb::velocypack::Builder&) const;

  /// @brief Build a velocypack containing all relevant information
  ///        for DBServer traverser engines.
  void buildEngineInfo(arangodb::velocypack::Builder&) const;

  bool vertexHasFilter(uint64_t) const;

  bool evaluateEdgeExpression(arangodb::velocypack::Slice,
                              StringRef vertexId, uint64_t,
                              size_t) const;

  bool evaluateVertexExpression(arangodb::velocypack::Slice, uint64_t) const;

  EdgeCursor* nextCursor(ManagedDocumentResult*, StringRef vid, uint64_t);

  void linkTraverser(arangodb::traverser::ClusterTraverser*);

  double estimateCost(size_t& nrItems) const;

 private:

  EdgeCursor* nextCursorLocal(ManagedDocumentResult*,
                              StringRef vid, uint64_t,
                              std::vector<LookupInfo>&);

  EdgeCursor* nextCursorCoordinator(StringRef vid, uint64_t);
};

}
}
#endif
