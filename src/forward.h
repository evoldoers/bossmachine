#ifndef FORWARD_INCLUDED
#define FORWARD_INCLUDED

#include "dpmatrix.h"

namespace MachineBoss {

template<class IndexMapper>
class MappedForwardMatrix : public DPMatrix<IndexMapper> {
private:
  void fill (StateIndex startState);
public:
  MappedForwardMatrix (const EvaluatedMachine&, const SeqPair&);
  MappedForwardMatrix (const EvaluatedMachine&, const SeqPair&, const Envelope&);
  MappedForwardMatrix (const EvaluatedMachine&, const SeqPair&, const Envelope&, StateIndex startState);
  double logLike() const;
};

class ForwardMatrix : public MappedForwardMatrix<IdentityIndexMapper> {
public:
  ForwardMatrix (const EvaluatedMachine&, const SeqPair&);
  ForwardMatrix (const EvaluatedMachine&, const SeqPair&, const Envelope&);
  ForwardMatrix (const EvaluatedMachine&, const SeqPair&, const Envelope&, StateIndex startState);
  MachinePath samplePath (const Machine&, mt19937&) const;
  MachinePath samplePath (const Machine&, StateIndex, mt19937&) const;
};

typedef MappedForwardMatrix<RollingOutputIndexMapper> RollingOutputForwardMatrix;

#include "forward.defs.h"

}  // end namespace

#endif /* FORWARD_INCLUDED */
