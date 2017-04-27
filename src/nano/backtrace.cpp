#include "../logger.h"
#include "backtrace.h"

BackwardTraceMatrix::BackwardTraceMatrix (const EvaluatedMachine& eval, const GaussianModelParams& modelParams, const Trace& trace, const TraceParams& traceParams) :
  TraceDPMatrix (eval, modelParams, trace, traceParams),
  nullTrans_rbegin (nullTrans().rbegin()),
  nullTrans_rend (nullTrans().rend())
{
  cell(outLen,eval.endState()) = 0;
  for (auto iter = nullTrans_rbegin; iter != nullTrans_rend; ++iter)
    log_accum_exp (cell(outLen,(*iter).src), cell(outLen,(*iter).dest) + (*iter).logWeight);

  for (OutputIndex outPos = outLen - 1; outPos >= 0; --outPos) {
    for (OutputToken outTok = 1; outTok < nOutToks; ++outTok) {
      const double llEmit = logEmitProb(outPos+1,outTok);
      for (const auto& it: transByOut[outTok])
	log_accum_exp (cell(outPos,it.src), cell(outPos+1,it.dest) + it.logWeight + llEmit);
    }

    for (auto iter = nullTrans_rbegin; iter != nullTrans_rend; ++iter)
      log_accum_exp (cell(outPos,(*iter).src), cell(outPos,(*iter).dest) + (*iter).logWeight);
  }
  LogThisAt(8,"Backward matrix:" << endl << toJsonString());
}

double BackwardTraceMatrix::logLike() const {
  return cell (0, eval.startState());
}

void BackwardTraceMatrix::getMachineCounts (const ForwardTraceMatrix& fwd, MachineCounts& counts) const {
  const double llFinal = fwd.logLike();

  for (const auto& it: nullTrans())
    counts.count[it.src][it.transIndex] += exp (fwd.cell(0,it.src) + it.logWeight + cell(0,it.dest) - llFinal);

  for (OutputIndex outPos = 1; outPos <= outLen; ++outPos) {
    for (OutputToken outTok = 1; outTok < nOutToks; ++outTok) {
      const double llEmit = logEmitProb(outPos,outTok);
      for (const auto& it: transByOut[outTok])
	counts.count[it.src][it.transIndex] += exp (fwd.cell(outPos-1,it.src) + it.logWeight + llEmit + cell(outPos,it.dest) - llFinal);
    }

    for (const auto& it: transByOut.front())
      counts.count[it.src][it.transIndex] += exp (fwd.cell(outPos,it.src) + it.logWeight + cell(outPos,it.dest) - llFinal);
  }
}

void BackwardTraceMatrix::getGaussianCounts (const ForwardTraceMatrix& fwd, vguard<GaussianCounts>& counts) const {
  const double llFinal = fwd.logLike();

  for (OutputIndex outPos = 1; outPos <= outLen; ++outPos)
    for (OutputToken outTok = 1; outTok < nOutToks; ++outTok) {
      const double llEmit = logEmitProb(outPos,outTok);
      const auto& sample = moments.sample[outPos-1];
      for (const auto& it: transByOut[outTok])
	counts[outTok-1].inc (sample, exp (fwd.cell(outPos-1,it.src) + it.logWeight + llEmit + cell(outPos,it.dest) - llFinal));
    }
}
