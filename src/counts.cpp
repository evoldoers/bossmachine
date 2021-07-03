#include <gsl/gsl_vector.h>
#include <gsl/gsl_multimin.h>
#include "counts.h"
#include "backward.h"
#include "util.h"
#include "logger.h"

// Prefix for sqrt-transformed parameters
#define TransformedParamPrefix "$x"

// GSL multidimensional optimization parameters
#define StepSize 0.1
#define LineSearchTolerance 1e-4
#define EpsilonAbsolute 1e-3
#define MaxIterations 100

// log levels
#define ParamTransformLogLevel 9
#define ObjectiveFunctionLogLevel 8
#define OptimizationParamsLogLevel 7

using namespace MachineBoss;

MachineCounts::MachineCounts()
{ }

MachineCounts::MachineCounts (const EvaluatedMachine& machine) {
  init (machine);
}

MachineCounts::MachineCounts (const EvaluatedMachine& machine, const SeqPair& seqPair)
{
  init (machine);
  (void) add (machine, seqPair);
}

MachineCounts::MachineCounts (const EvaluatedMachine& machine, const SeqPairList& seqPairList, const list<Envelope>& envelopes)
{
  init (machine);
  auto env = envelopes.begin();
  for (const auto& seqPair: seqPairList.seqPairs)
    (void) add (machine, seqPair, env == envelopes.end() ? Envelope(seqPair) : *(env++));
}

void MachineCounts::init (const EvaluatedMachine& machine) {
  loglike = 0;
  count = vguard<vguard<double> > (machine.nStates());
  for (StateIndex s = 0; s < machine.nStates(); ++s)
    count[s].resize (machine.state[s].nTransitions, 0.);
}

double MachineCounts::add (const EvaluatedMachine& machine, const SeqPair& seqPair) {
  const Envelope env (seqPair);
  return add (machine, seqPair, env);
}

double MachineCounts::add (const EvaluatedMachine& machine, const SeqPair& seqPair, const Envelope& env) {
  const ForwardMatrix forward (machine, seqPair, env);
  const BackwardMatrix backward (machine, seqPair, env);
  backward.getCounts (forward, *this);
  const double result = forward.logLike();
  loglike += result;
  return result;
}

MachineCounts& MachineCounts::operator+= (const MachineCounts& counts) {
  for (StateIndex s = 0; s < count.size(); ++s)
    for (size_t t = 0; t < count[s].size(); ++t)
      count[s][t] += counts.count[s][t];
  return *this;
}

void MachineCounts::writeJson (ostream& outs) const {
  vguard<string> s;
  for (const auto& c: count)
    s.push_back (string("[") + to_string_join (c, ",") + "]");
  outs << "[" << join (s, ",\n ") << "]" << endl;
}

void MachineCounts::writeParamCountsJson (ostream& outs, const Machine& machine, const ParamAssign& prob) const {
  const auto pc = paramCounts (machine, prob);
  outs << "{";
  size_t n = 0;
  for (const auto& name_count: pc)
    outs << (n++ ? "," : "") << "\"" << escaped_str(name_count.first) << "\":" << name_count.second;
  outs << "}";
}

map<string,double> MachineCounts::paramCounts (const Machine& machine, const ParamAssign& prob) const {
  map<string,double> paramCount;
  Assert (count.size() == machine.state.size(), "Number of states mismatch");
  for (StateIndex s = 0; s < machine.nStates(); ++s) {
    auto transIter = machine.state[s].trans.begin();
    Assert (count[s].size() == machine.state[s].trans.size(), "State size mismatch");
    for (auto& c: count[s]) {
      auto& trans = *(transIter++);
      const auto transParams = WeightAlgebra::params (trans.weight, ParamDefs());
      const double w = WeightAlgebra::eval (trans.weight, prob.defs);
      for (auto& p: transParams) {
	const auto deriv = WeightAlgebra::deriv (trans.weight, ParamDefs(), p);
	paramCount[p] += c * WeightAlgebra::eval (deriv, prob.defs) * WeightAlgebra::asDouble (prob.defs.at(p)) / w;
      }
    }
  }
  return paramCount;
}

WeightExpr makeSquareFunc (const string& trParam) {
  WeightExpr tr = WeightAlgebra::param (trParam);
  return WeightAlgebra::multiply (tr, tr);
}

WeightExpr makeExpFunc (const string& trParam) {
  return WeightAlgebra::expOf (WeightAlgebra::minus (makeSquareFunc (trParam)));
}

MachineObjective::MachineObjective (const Machine& machine, const MachineCounts& counts, const Constraints& cons, const Params& constants) :
  constraints (machine.cons.combine (cons)),
  constantDefs (machine.funcs.combine (constants).defs),
  objective (WeightAlgebra::zero())
{
  for (StateIndex s = 0; s < machine.state.size(); ++s) {
    EvaluatedMachineState::TransIndex t = 0;
    for (TransList::const_iterator iter = machine.state[s].trans.begin();
	 iter != machine.state[s].trans.end(); ++iter, ++t) {
      const WeightExpr term = WeightAlgebra::multiply (WeightAlgebra::doubleConstant (counts.count[s][t]),
						       WeightAlgebra::logOf ((*iter).weight));
      objective = WeightAlgebra::subtract (objective, term);
    }
  }

  // p_i = (1 - exp(-x_i^2)) \prod_{k=1}^{i-1} exp(-x_k^2)
  const set<string> p = WeightAlgebra::params (objective, ParamDefs());
  int trIdx = 0;
  auto makeTransformedParamName = [&] (const string& param) -> string {
    string trParam;
    do
      trParam = string(TransformedParamPrefix) + to_string(++trIdx);
    while (p.count(trParam));
    transformedParamIndex[param] = transformedParam.size();
    transformedParam.push_back (trParam);
    return trParam;
  };
  for (const auto& c: constraints.norm) {
    WeightExpr notPrev = WeightAlgebra::one();
    for (size_t n = 0; n < c.size(); ++n) {
      const string& cParam = c[n];
      if (n + 1 == c.size())
	paramTransformDefs[cParam] = notPrev;
      else {
	const string trParam = makeTransformedParamName (cParam);
	WeightExpr notThis = makeExpFunc (trParam);
	paramTransformDefs[cParam] = WeightAlgebra::multiply (notPrev, WeightAlgebra::negate (notThis));
	notPrev = WeightAlgebra::multiply (notPrev, notThis);
      }
    }
  }

  for (const auto& pParam: constraints.prob)
    paramTransformDefs[pParam] = makeExpFunc (makeTransformedParamName (pParam));

  for (const auto& rParam: constraints.rate)
    paramTransformDefs[rParam] = makeSquareFunc (makeTransformedParamName (rParam));

  if (LoggingThisAt(ParamTransformLogLevel))
    for (const auto& p_d: paramTransformDefs)
      LogThisAt(ParamTransformLogLevel,"Mapping " << p_d.first << " to " << WeightAlgebra::toString (p_d.second, ParamDefs()) << endl);

  allDefs = constantDefs;
  allDefs.insert (paramTransformDefs.begin(), paramTransformDefs.end());

  deriv.reserve (transformedParam.size());
  for (const auto& p: transformedParam)
    deriv.push_back (WeightAlgebra::deriv (objective, allDefs, p));

  LogThisAt (ObjectiveFunctionLogLevel, toString());
}

string MachineObjective::toString() const {
  string s = string("E = ") + WeightAlgebra::toString(objective,allDefs) + "\n";
  for (size_t n = 0; n < transformedParam.size(); ++n)
    s += "dE/d" + transformedParam[n] + " = " + WeightAlgebra::toString(deriv[n],allDefs) + "\n";
  return s;
}

Params gsl_vector_to_params (const gsl_vector *v, const MachineObjective& ml) {
  Params p;
  p.defs = ml.allDefs;
  for (size_t n = 0; n < ml.transformedParam.size(); ++n)
    p.defs[ml.transformedParam[n]] = WeightExpr (WeightAlgebra::doubleConstant (gsl_vector_get (v, n)));
  return p;
}

double gsl_machine_objective (const gsl_vector *v, void *voidML)
{
  const MachineObjective& ml (*((MachineObjective*)voidML));
  const Params pv = gsl_vector_to_params (v, ml);

  const double f = WeightAlgebra::eval (ml.objective, pv.defs);

  LogThisAt (OptimizationParamsLogLevel, JsonLoader<Params>::toJsonString(pv) << endl);
  LogThisAt (ObjectiveFunctionLogLevel, "gsl_machine_objective(" << to_string_join(gsl_vector_to_stl(v)) << ") = " << f << endl);

  return f;
}

void gsl_machine_objective_deriv (const gsl_vector *v, void *voidML, gsl_vector *df)
{
  const MachineObjective& ml (*((MachineObjective*)voidML));
  const Params pv = gsl_vector_to_params (v, ml);

  for (size_t n = 0; n < ml.transformedParam.size(); ++n)
    gsl_vector_set (df, n, WeightAlgebra::eval (ml.deriv[n], pv.defs));

  const vguard<double> v_stl = gsl_vector_to_stl(v), df_stl = gsl_vector_to_stl(df);
  LogThisAt (ObjectiveFunctionLogLevel, "gsl_machine_objective_deriv(" << to_string_join(v_stl) << ") = (" << to_string_join(df_stl) << ")" << endl);
}

void gsl_machine_objective_with_deriv (const gsl_vector *x, void *voidML, double *f, gsl_vector *df)
{
  *f = gsl_machine_objective (x, voidML);
  gsl_machine_objective_deriv (x, voidML, df);
}

Params MachineObjective::optimize (const Params& seed) const {
  gsl_vector *v;
  gsl_multimin_function_fdf func;
  func.n = transformedParam.size();
  func.f = gsl_machine_objective;
  func.df = gsl_machine_objective_deriv;
  func.fdf = gsl_machine_objective_with_deriv;
  func.params = (void*) this;

  gsl_vector* x = gsl_vector_alloc (func.n);
  // p_i = (1 - z_i) \prod_{k=1}^{i-1} z_k
  // where z_i = exp(-x_i^2)
  // \prod_{k=1}^{i-1} z_k = 1 - \sum_{k=1}^{i-1} p_k
  // so z_i = 1 - p_i / (1 - \sum_{k=1}^{i-1} p_k)
  // x_i = sqrt(-log z_i)
  for (const auto& c: constraints.norm) {
    double pSum = 0;
    for (size_t n = 0; n + 1 < c.size(); ++n) {
      const string& cParam = c[n];
      const double p = WeightAlgebra::asDouble (seed.defs.at(cParam));
      const double z = 1 - p / (1 - pSum);
      const double val = sqrt (-log (z));
      pSum += p;
      gsl_vector_set (x, transformedParamIndex.at(cParam), val);
      LogThisAt(ParamTransformLogLevel,"Setting " << transformedParam[transformedParamIndex.at(cParam)] << " to " << val << endl);
    }
  }
  for (const auto& pParam: constraints.prob) {
      const double p = WeightAlgebra::asDouble (seed.defs.at(pParam));
      const double val = sqrt (-log (p));
      gsl_vector_set (x, transformedParamIndex.at(pParam), val);
      LogThisAt(ParamTransformLogLevel,"Setting " << transformedParam[transformedParamIndex.at(pParam)] << " to " << val << endl);
  }
  for (const auto& rParam: constraints.rate) {
      const double r = WeightAlgebra::asDouble (seed.defs.at(rParam));
      const double val = sqrt (r);
      gsl_vector_set (x, transformedParamIndex.at(rParam), val);
      LogThisAt(ParamTransformLogLevel,"Setting " << transformedParam[transformedParamIndex.at(rParam)] << " to " << val << endl);
  }

  const gsl_multimin_fdfminimizer_type *T = gsl_multimin_fdfminimizer_vector_bfgs2;
  gsl_multimin_fdfminimizer *s = gsl_multimin_fdfminimizer_alloc (T, func.n);

  gsl_multimin_fdfminimizer_set (s, &func, x, StepSize, LineSearchTolerance);
  
  size_t iter = 0;
  int status;
  do
    {
      iter++;
      status = gsl_multimin_fdfminimizer_iterate (s);

      const vguard<double> x_stl = gsl_vector_to_stl(s->x);
      LogThisAt (OptimizationParamsLogLevel, "iteration #" << iter << ": x=(" << to_string_join(x_stl) << ")" << endl);

      if (status)
        break;

      status = gsl_multimin_test_gradient (s->gradient, EpsilonAbsolute);
    }
  while (status == GSL_CONTINUE && iter < MaxIterations);

  const Params finalTransformedParams = gsl_vector_to_params (s->x, *this);
  Params finalParams = seed;
  for (const auto& pt: paramTransformDefs)
    finalParams.defs[pt.first] = WeightAlgebra::doubleConstant (WeightAlgebra::eval (pt.second, finalTransformedParams.defs));
  
  gsl_multimin_fdfminimizer_free (s);
  gsl_vector_free (x);

  return finalParams;
}
