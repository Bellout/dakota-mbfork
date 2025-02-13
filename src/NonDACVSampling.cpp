/*  _______________________________________________________________________

    DAKOTA: Design Analysis Kit for Optimization and Terascale Applications
    Copyright 2014-2022
    National Technology & Engineering Solutions of Sandia, LLC (NTESS).
    This software is distributed under the GNU Lesser General Public License.
    For more information, see the README file in the top Dakota directory.
    _______________________________________________________________________ */

//- Class:	 NonDACVSampling
//- Description: Implementation code for NonDACVSampling class
//- Owner:       Mike Eldred
//- Checked by:
//- Version:

#include "dakota_system_defs.hpp"
#include "dakota_data_io.hpp"
//#include "dakota_tabular_io.hpp"
#include "DakotaModel.hpp"
#include "DakotaResponse.hpp"
#include "NonDACVSampling.hpp"
#include "ProblemDescDB.hpp"
#include "ActiveKey.hpp"
#include "DakotaIterator.hpp"

static const char rcsId[]="@(#) $Id: NonDACVSampling.cpp 7035 2010-10-22 21:45:39Z mseldre $";

namespace Dakota {


/** This constructor is called for a standard letter-envelope iterator 
    instantiation.  In this case, set_db_list_nodes has been called and 
    probDescDB can be queried for settings from the method specification. */
NonDACVSampling::
NonDACVSampling(ProblemDescDB& problem_db, Model& model):
  NonDNonHierarchSampling(problem_db, model), multiStartACV(true)
{
  mlmfSubMethod = problem_db.get_ushort("method.sub_method");

  if (maxFunctionEvals == SZ_MAX) // accuracy constraint (convTol)
    optSubProblemForm = N_VECTOR_LINEAR_OBJECTIVE;
  else                     // budget constraint (maxFunctionEvals)
    // truthFixedByPilot is a user-specified option for fixing the number of
    // HF samples (to those in the pilot).  In this case, equivHF budget is
    // allocated by optimizing r* for fixed N.
    optSubProblemForm = (truthFixedByPilot && pilotMgmtMode != OFFLINE_PILOT) ?
      R_ONLY_LINEAR_CONSTRAINT : N_VECTOR_LINEAR_CONSTRAINT;

  if (outputLevel >= DEBUG_OUTPUT)
    Cout << "ACV sub-method selection = " << mlmfSubMethod
	 << " sub-method formulation = "  << optSubProblemForm
	 << " sub-problem solver = "      << optSubProblemSolver << std::endl;
}


NonDACVSampling::~NonDACVSampling()
{ }


/** The primary run function manages the general case: a hierarchy of model 
    forms (from the ordered model fidelities within a HierarchSurrModel), 
    each of which may contain multiple discretization levels. */
void NonDACVSampling::core_run()
{
  /*
  switch (mlmfSubMethod) {
  case SUBMETHOD_ACV_IS:  case SUBMETHOD_ACV_MF:
    approximate_control_variate(); break;
  //case SUBMETHOD_ACV_KL:
    //for (k) for (l) approximate_control_variate(...); ???
  }
  */
  if (mlmfSubMethod == SUBMETHOD_ACV_KL) {
    Cerr << "Error: ACV KL not yet implemented." << std::endl;
    abort_handler(METHOD_ERROR);
  }

  // Initialize for pilot sample
  numSamples = pilotSamples[numApprox]; // last in pilot array

  switch (pilotMgmtMode) {
  case  ONLINE_PILOT: // iterated ACV (default)
    approximate_control_variate();                  break;
  case OFFLINE_PILOT: // computes perf for offline pilot/Oracle correlation
    approximate_control_variate_offline_pilot();    break;
  case PILOT_PROJECTION: // for algorithm assessment/selection
    approximate_control_variate_pilot_projection(); break;
  }
}


/** This function performs control variate MC across two combinations of 
    model form and discretization level. */
void NonDACVSampling::approximate_control_variate()
{
  // retrieve cost estimates across soln levels for a particular model form
  IntRealVectorMap sum_H;  IntRealMatrixMap sum_L_baselineH, sum_LH;
  IntRealSymMatrixArrayMap sum_LL;
  RealVector sum_HH, avg_eval_ratios;  RealMatrix var_L;
  //SizetSymMatrixArray N_LL;
  initialize_acv_sums(sum_L_baselineH, sum_H, sum_LL, sum_LH, sum_HH);
  size_t hf_form_index, hf_lev_index;  hf_indices(hf_form_index, hf_lev_index);
  SizetArray& N_H_actual = NLevActual[hf_form_index][hf_lev_index];
  size_t&     N_H_alloc  =  NLevAlloc[hf_form_index][hf_lev_index];
  N_H_actual.assign(numFunctions, 0);  N_H_alloc = 0;
  //initialize_acv_counts(N_H_actual, N_LL);
  //initialize_acv_covariances(covLL, covLH, varH);

  // Initialize for pilot sample
  //size_t hf_shared_pilot = numSamples, start=0,
  //  lf_shared_pilot = find_min(pilotSamples, start, numApprox-1);

  Real avg_hf_target = 0.;
  while (numSamples && mlmfIter <= maxIterations) {

    // --------------------------------------------------------------------
    // Evaluate shared increment and update correlations, {eval,EstVar}_ratios
    // --------------------------------------------------------------------
    shared_increment(mlmfIter); // spans ALL models, blocking
    accumulate_acv_sums(sum_L_baselineH, /*sum_L_baselineL,*/ sum_H, sum_LL,
			sum_LH, sum_HH, N_H_actual);//, N_LL);
    N_H_alloc += (backfillFailures && mlmfIter) ?
      one_sided_delta(N_H_alloc, avg_hf_target) : numSamples;
    // While online cost recovery could be continuously updated, we restrict
    // to the pilot and do not not update after iter 0.  We could potentially
    // update cost for shared samples, mirroring the covariance updates.
    if (onlineCost && mlmfIter == 0) recover_online_cost(sequenceCost);
    increment_equivalent_cost(numSamples, sequenceCost, 0, numSteps,
			      equivHFEvals);
    // allow pilot to vary for C vs c
    // *** TO DO: numSamples logic after pilot (mlmfIter >= 1)
    // *** Will likely require _baselineL and _baselineH
    //if (mlmfIter == 0 && lf_shared_pilot > hf_shared_pilot) {
    //  numSamples = lf_shared_pilot - hf_shared_pilot;
    //  shared_approx_increment(mlmfIter); // spans all approx models
    //  accumulate_acv_sums(sum_L_baselineL, sum_LL,//_baselineL,
    //                      N_L_baselineL);
    //  increment_equivalent_cost(numSamples, sequenceCost, 0, numApprox,
    //			          equivHFEvals);
    //}

    const RealMatrix&         sum_L_1  = sum_L_baselineH[1];
    const RealVector&         sum_H_1  = sum_H[1];
    const RealSymMatrixArray& sum_LL_1 = sum_LL[1];
    compute_variance(sum_H_1, sum_HH, N_H_actual, varH);
    if (mlmfIter == 0) compute_L_variance(sum_L_1, sum_LL_1, N_H_actual, var_L);
    compute_LH_covariance(sum_L_1/*baseH*/, sum_H_1, sum_LH[1],
			  N_H_actual, covLH);
    compute_LL_covariance(sum_L_1/*baseL*/, sum_LL_1, N_H_actual, covLL);
    //Cout << "var_H:\n"<< var_H << "cov_LH:\n"<< cov_LH << "cov_LL:\n"<<cov_LL;

    // compute the LF/HF evaluation ratios from shared samples and compute
    // ratio of MC and ACV mean sq errors (which incorporates anticipated
    // variance reduction from application of avg_eval_ratios).
    compute_ratios(var_L, sequenceCost, avg_eval_ratios, avg_hf_target,
		   avgEstVar, avgEstVarRatio);
    ++mlmfIter;
  }

  // Only QOI_STATISTICS requires application of oversample ratios and
  // estimation of moments; ESTIMATOR_PERFORMANCE can bypass this expense.
  if (finalStatsType == QOI_STATISTICS)
    approx_increments(sum_L_baselineH, sum_H, sum_LL, sum_LH, N_H_actual,
		      N_H_alloc, avg_eval_ratios, avg_hf_target);
  else
    // N_H is final --> do not compute any deltaNActualHF (from maxIter exit)
    update_projected_lf_samples(avg_hf_target, avg_eval_ratios, N_H_actual,
				N_H_alloc, deltaEquivHF);
}


/** This function performs control variate MC across two combinations of 
    model form and discretization level. */
void NonDACVSampling::approximate_control_variate_offline_pilot()
{
  RealVector sum_H_pilot(numFunctions), sum_HH_pilot(numFunctions);
  RealMatrix sum_L_pilot(numFunctions, numApprox),
    sum_LH_pilot(numFunctions, numApprox), var_L;
  RealSymMatrixArray sum_LL_pilot(numFunctions);
  for (size_t qoi=0; qoi<numFunctions; ++qoi)
    sum_LL_pilot[qoi].shape(numApprox);
  SizetArray N_shared_pilot;  //SizetSymMatrixArray N_LL_pilot;
  N_shared_pilot.assign(numFunctions, 0);
  //initialize_acv_counts(N_shared_pilot, N_LL_pilot);
  // ------------------------------------------------------------
  // Compute var L,H & covar LL,LH from (oracle) pilot treated as "offline" cost
  // ------------------------------------------------------------
  // Initialize for pilot sample
  size_t hf_shared_pilot = numSamples;
  shared_increment(mlmfIter); // spans ALL models, blocking
  accumulate_acv_sums(sum_L_pilot, sum_H_pilot, sum_LL_pilot, sum_LH_pilot,
		      sum_HH_pilot, N_shared_pilot);//, N_LL_pilot);
  if (onlineCost) recover_online_cost(sequenceCost);
  //increment_equivalent_cost(numSamples,sequenceCost,0,numSteps,equivHFEvals);
  compute_variance(sum_H_pilot, sum_HH_pilot, N_shared_pilot, varH);
  compute_L_variance(sum_L_pilot, sum_LL_pilot, N_shared_pilot, var_L);
  compute_LH_covariance(sum_L_pilot, sum_H_pilot, sum_LH_pilot,
			N_shared_pilot, covLH);
  compute_LL_covariance(sum_L_pilot, sum_LL_pilot,
			/*N_LL_pilot*/N_shared_pilot, covLL);
  //Cout << "var_H:\n"<< var_H << "cov_LH:\n"<< cov_LH << "cov_LL:\n"<<cov_LL;

  // -----------------------------------
  // Compute "online" sample increments:
  // -----------------------------------
  IntRealVectorMap sum_H;  IntRealMatrixMap sum_L_baselineH, sum_LH;
  IntRealSymMatrixArrayMap sum_LL;
  RealVector sum_HH, avg_eval_ratios;
  //SizetSymMatrixArray N_LL;
  initialize_acv_sums(sum_L_baselineH, sum_H, sum_LL, sum_LH, sum_HH);
  size_t hf_form_index, hf_lev_index;  hf_indices(hf_form_index, hf_lev_index);
  SizetArray& N_H_actual = NLevActual[hf_form_index][hf_lev_index];
  size_t&     N_H_alloc  =  NLevAlloc[hf_form_index][hf_lev_index];
  N_H_actual.assign(numFunctions, 0);  N_H_alloc = 0;
  //initialize_acv_counts(N_H_actual, N_LL);
  //initialize_acv_covariances(covLL, covLH, varH);
  Real avg_hf_target = 0.;

  // compute the LF/HF evaluation ratios from shared samples and compute
  // ratio of MC and ACV mean sq errors (which incorporates anticipated
  // variance reduction from application of avg_eval_ratios).
  compute_ratios(var_L, sequenceCost, avg_eval_ratios, avg_hf_target,
		 avgEstVar, avgEstVarRatio);
  ++mlmfIter;

  // -----------------------------------
  // Perform "online" sample increments:
  // -----------------------------------
  // at least 2 samples reqd for variance (+ resetting allSamples from pilot)
  numSamples = std::max(numSamples, (size_t)2);
  shared_increment(mlmfIter); // spans ALL models, blocking
  accumulate_acv_sums(sum_L_baselineH, /*sum_L_baselineL,*/ sum_H, sum_LL,
		      sum_LH, sum_HH, N_H_actual);//, N_LL);
  N_H_alloc += numSamples;
  increment_equivalent_cost(numSamples, sequenceCost, 0, numSteps,equivHFEvals);
  // allow pilot to vary for C vs c

  // Only QOI_STATISTICS requires application of oversample ratios and
  // estimation of moments; ESTIMATOR_PERFORMANCE can bypass this expense.
  if (finalStatsType == QOI_STATISTICS)
    approx_increments(sum_L_baselineH, sum_H, sum_LL, sum_LH, N_H_actual,
		      N_H_alloc, avg_eval_ratios, avg_hf_target);
  else
    // N_H is converged from offline pilot --> do not compute deltaNActualHF
    update_projected_lf_samples(avg_hf_target, avg_eval_ratios, N_H_actual,
				N_H_alloc, deltaEquivHF);
}


/** This function performs control variate MC across two combinations of 
    model form and discretization level. */
void NonDACVSampling::approximate_control_variate_pilot_projection()
{
  RealVector sum_H(numFunctions), sum_HH(numFunctions), avg_eval_ratios;
  RealMatrix sum_L_baselineH(numFunctions, numApprox),
    sum_LH(numFunctions, numApprox), var_L;
  RealSymMatrixArray sum_LL(numFunctions);
  for (size_t qoi=0; qoi<numFunctions; ++qoi)
    sum_LL[qoi].shape(numApprox);
  size_t hf_form_index, hf_lev_index;  hf_indices(hf_form_index, hf_lev_index);
  SizetArray& N_H_actual = NLevActual[hf_form_index][hf_lev_index];
  size_t&     N_H_alloc  =  NLevAlloc[hf_form_index][hf_lev_index];
  //SizetSymMatrixArray N_LL;  initialize_acv_counts(N_H_actual, N_LL);
  N_H_actual.assign(numFunctions, 0);  N_H_alloc = 0;
  Real avg_hf_target = 0.;

  // Initialize for pilot sample
  size_t hf_shared_pilot = numSamples;
  //, start=0, lf_shared_pilot = find_min(pilotSamples, start, numApprox-1);

  // --------------------------------------------------------------------
  // Evaluate shared increment and update correlations, {eval,EstVar}_ratios
  // --------------------------------------------------------------------
  shared_increment(mlmfIter); // spans ALL models, blocking
  accumulate_acv_sums(sum_L_baselineH, /*sum_L_baselineL,*/ sum_H, sum_LL,
		      sum_LH, sum_HH, N_H_actual);//, N_LL);
  N_H_alloc += numSamples;
  if (onlineCost) recover_online_cost(sequenceCost);
  increment_equivalent_cost(numSamples, sequenceCost, 0, numSteps,equivHFEvals);
  // allow pilot to vary for C vs c

  compute_variance(sum_H, sum_HH, N_H_actual, varH);
  compute_L_variance(sum_L_baselineH, sum_LL, N_H_actual, var_L);
  compute_LH_covariance(sum_L_baselineH, sum_H, sum_LH, N_H_actual, covLH);
  compute_LL_covariance(sum_L_baselineH/*baseL*/, sum_LL, N_H_actual, covLL);
  //Cout << "var_H:\n"<< var_H << "cov_LH:\n"<< cov_LH << "cov_LL:\n"<<cov_LL;

  // -----------------------------------
  // Compute "online" sample increments:
  // -----------------------------------
  // compute the LF/HF evaluation ratios from shared samples and compute
  // ratio of MC and ACV mean sq errors (which incorporates anticipated
  // variance reduction from application of avg_eval_ratios).
  compute_ratios(var_L, sequenceCost, avg_eval_ratios, avg_hf_target,
		 avgEstVar, avgEstVarRatio);
  ++mlmfIter;

  // No LF increments or final moments for pilot projection
  update_projected_samples(avg_hf_target, avg_eval_ratios, N_H_actual,
			   N_H_alloc, deltaNActualHF, deltaEquivHF);
  // No need for updating estimator variance given deltaNActualHF since
  // NonDNonHierarchSampling::nonhierarch_numerical_solution() recovers N*
  // from the numerical solve and computes projected avgEstVar{,Ratio}
}


void NonDACVSampling::
approx_increments(IntRealMatrixMap& sum_L_baselineH, IntRealVectorMap& sum_H,
		  IntRealSymMatrixArrayMap& sum_LL,  IntRealMatrixMap& sum_LH,
		  const SizetArray& N_H_actual, size_t N_H_alloc,
		  const RealVector& avg_eval_ratios, Real avg_hf_target)
{
  // ---------------------------------------------------------------
  // Compute N_L increments based on eval ratio applied to final N_H
  // ---------------------------------------------------------------
  // Note: these results do not affect the iteration above and can be performed
  // after N_H has converged, which simplifies maxFnEvals / convTol logic
  // (no need to further interrogate these throttles below)

  // maxIterations == 0 is no longer reserved for the pilot only case.
  // See notes in NonDMultifidelitySampling::multifidelity_mc().

  // define approx_sequence in decreasing r_i order, directionally consistent
  // with default approx indexing for well-ordered models
  // > approx 0 is lowest fidelity --> lowest corr,cost --> highest r_i
  SizetArray approx_sequence;  bool descending = true;
  ordered_approx_sequence(avg_eval_ratios, approx_sequence, descending);

  IntRealMatrixMap sum_L_refined = sum_L_baselineH;//baselineL;
  Sizet2DArray N_L_actual_shared;  inflate(N_H_actual, N_L_actual_shared);
  Sizet2DArray N_L_actual_refined = N_L_actual_shared;
  SizetArray   N_L_alloc_refined;  inflate(N_H_alloc, N_L_alloc_refined);
  size_t start, end;
  for (end=numApprox; end>0; --end) {
    // *** TO DO NON_BLOCKING: PERFORM 2ND PASS ACCUMULATE AFTER 1ST PASS LAUNCH
    start = (mlmfSubMethod == SUBMETHOD_ACV_IS) ? end - 1 : 0;
    if (acv_approx_increment(avg_eval_ratios, N_L_actual_refined,
			     N_L_alloc_refined, avg_hf_target, mlmfIter,
			     approx_sequence, start, end)) {
      // ACV_IS samples on [approx-1,approx) --> sum_L_refined
      // ACV_MF samples on [0, approx)       --> sum_L_refined
      accumulate_acv_sums(sum_L_refined, N_L_actual_refined, approx_sequence,
			  start, end);
      increment_equivalent_cost(numSamples, sequenceCost, approx_sequence,
				start, end, equivHFEvals);
    }
  }

  // -----------------------------------------------------------
  // Compute/apply control variate parameter to estimate moments
  // -----------------------------------------------------------
  RealMatrix H_raw_mom(numFunctions, 4);
  acv_raw_moments(sum_L_baselineH, sum_L_refined, sum_H, sum_LL, sum_LH,
		  avg_eval_ratios, N_H_actual, N_L_actual_refined, H_raw_mom);
  // Convert uncentered raw moment estimates to final moments (central or std)
  convert_moments(H_raw_mom, momentStats);
  // post final sample counts into format for final results reporting
  finalize_counts(N_L_actual_refined, N_L_alloc_refined);
}


bool NonDACVSampling::
acv_approx_increment(const RealVector& avg_eval_ratios,
		     const Sizet2DArray& N_L_actual_refined,
		     SizetArray& N_L_alloc_refined, Real hf_target,
		     size_t iter, const SizetArray& approx_sequence,
		     size_t start, size_t end)
{
  // Update LF samples based on evaluation ratio
  //   r = N_L/N_H -> N_L = r * N_H -> delta = N_L - N_H = (r-1) * N_H
  // Notes:
  // > the sample increment for the approx range is determined by approx[end-1]
  //   (helpful to refer to Figure 2(b) in ACV paper, noting index differences)
  // > N_L is updated prior to each call to approx_increment (*** if BLOCKING),
  //   allowing use of one_sided_delta() with latest counts

  bool ordered = approx_sequence.empty();
  size_t approx = (ordered) ? end-1 : approx_sequence[end-1];
  Real lf_target = avg_eval_ratios[approx] * hf_target;
  if (backfillFailures) {
    Real lf_curr = average(N_L_actual_refined[approx]);
    numSamples = one_sided_delta(lf_curr, lf_target); // average
    if (outputLevel >= DEBUG_OUTPUT)
      Cout << "Approx samples (" << numSamples
	   << ") computed from delta between LF target = " << lf_target
	   << " and current average count = " << lf_curr << std::endl;
    size_t N_alloc = one_sided_delta(N_L_alloc_refined[approx], lf_target);
    increment_sample_range(N_L_alloc_refined, N_alloc, approx_sequence,
			   start, end);
  }
  else {
    size_t lf_curr = N_L_alloc_refined[approx];
    numSamples = one_sided_delta((Real)lf_curr, lf_target);
    if (outputLevel >= DEBUG_OUTPUT)
      Cout << "Approx samples (" << numSamples
	   << ") computed from delta between LF target " << lf_target
	   << " and current allocation = " << lf_curr << std::endl;
    increment_sample_range(N_L_alloc_refined, numSamples, approx_sequence,
			   start, end);
  }
  // the approximation sequence can be managed within one set of jobs using
  // a composite ASV with NonHierarchSurrModel
  return approx_increment(iter, approx_sequence, start, end);
}


void NonDACVSampling::
compute_ratios(const RealMatrix& var_L,     const RealVector& cost,
	       RealVector& avg_eval_ratios, Real& avg_hf_target,
	       Real& avg_estvar,            Real& avg_estvar_ratio)
{
  // --------------------------------------
  // Configure the optimization sub-problem
  // --------------------------------------

  // Set initial guess based either on MFMC analytic solution (iter == 0)
  // or warm started from previous solution (iter >= 1)
  if (mlmfIter == 0) {
    size_t hf_form_index, hf_lev_index; hf_indices(hf_form_index, hf_lev_index);
    SizetArray& N_H_actual = NLevActual[hf_form_index][hf_lev_index];
    size_t&     N_H_alloc  =  NLevAlloc[hf_form_index][hf_lev_index];
    // estVarIter0 only uses HF pilot since sum_L_shared / N_shared minus
    // sum_L_refined / N_refined are zero for CVs prior to sample refinement.
    // (This differs from MLMC EstVar^0 which uses pilot for all levels.)
    // Note: could revisit this for case of lf_shared_pilot > hf_shared_pilot.
    compute_mc_estimator_variance(varH, N_H_actual, estVarIter0);
    numHIter0 = N_H_actual;
    Real avg_N_H = (backfillFailures) ? average(N_H_actual) : N_H_alloc;
    // Modify budget to allow a feasible soln (var lower bnds: r_i > 1, N > N_H)
    // Can happen if shared pilot rolls up to exceed budget spec.
    Real budget             = (Real)maxFunctionEvals;
    bool budget_constrained = (maxFunctionEvals != SZ_MAX),
         budget_exhausted   = (equivHFEvals >= budget);
    //if (budget_exhausted) budget = equivHFEvals;

    if (budget_exhausted || convergenceTol >= 1.) { // no need for solve
      if (avg_eval_ratios.empty()) avg_eval_ratios.sizeUninitialized(numApprox);
      numSamples = 0; avg_eval_ratios = 1.; avg_hf_target = avg_N_H;
      avg_estvar = average(estVarIter0);    avg_estvar_ratio = 1.;
      return;
    }

    // compute initial estimate of r* from MFMC
    covariance_to_correlation_sq(covLH, var_L, varH, rho2LH);

    // Run a competition among analytic approaches for best initial guess:
    // > Option 1 is analytic MFMC: differs from ACV due to recursive pairing
    Real avg_estvar1, avg_estvar2, avg_hf_target1, avg_hf_target2;
    RealMatrix     eval_ratios1,     eval_ratios2;
    RealVector avg_eval_ratios1, avg_eval_ratios2;
    if (ordered_approx_sequence(rho2LH)) // for all QoI across all Approx
      mfmc_analytic_solution(rho2LH, cost, eval_ratios1);
    else // compute reordered MFMC for averaged rho; monotonic r not reqd
      mfmc_reordered_analytic_solution(rho2LH, cost, approxSequence,
				       eval_ratios1, false);
    //Cout << "MFMC eval_ratios:\n" << eval_ratios1 << std::endl;

    // > Option 2 is ensemble of independent two-model CVMCs, rescaled to an
    //   aggregate budget.  This is more ACV-like in the sense that it is not
    //   recursive, but it neglects the covariance C among approximations.
    //   It is also insensitive to model sequencing.
    cvmc_ensemble_solutions(rho2LH, cost, eval_ratios2);
    //Cout << "CVMC eval_ratios:\n" << eval_ratios2 << std::endl;
    average(eval_ratios1, 0, avg_eval_ratios1);
    average(eval_ratios2, 0, avg_eval_ratios2);

    // any rho2_LH re-ordering from MFMC initial guess can be ignored (later
    // gets replaced with r_i ordering for approx_increments() sampling)
    approxSequence.clear();
    bool mfmc_init;
    if (multiStartACV) { // Run numerical solns from both starting points
      if (budget_constrained) {
	scale_to_target(avg_N_H, cost, avg_eval_ratios1, avg_hf_target1);
	scale_to_target(avg_N_H, cost, avg_eval_ratios2, avg_hf_target2);
      }
      else {
	avg_hf_target1 = update_hf_target(avg_eval_ratios1, varH, estVarIter0);
	avg_hf_target2 = update_hf_target(avg_eval_ratios2, varH, estVarIter0);
      }
      Real avg_estvar_ratio1, avg_estvar_ratio2;
      size_t num_samp1, num_samp2;
      nonhierarch_numerical_solution(cost, approxSequence, avg_eval_ratios1,
				     avg_hf_target1, num_samp1, avg_estvar1,
				     avg_estvar_ratio1);
      nonhierarch_numerical_solution(cost, approxSequence, avg_eval_ratios2,
				     avg_hf_target2, num_samp2, avg_estvar2,
				     avg_estvar_ratio2);
      if (budget_constrained) // same cost, compare accuracy
	mfmc_init = (avg_estvar1 <= avg_estvar2);
      else                    // same accuracy, compare cost
	mfmc_init
	  = (compute_equivalent_cost(avg_hf_target1, avg_eval_ratios1, cost)
	  <= compute_equivalent_cost(avg_hf_target2, avg_eval_ratios2, cost));
      Cout << "\nACV best solution from ";
      if (mfmc_init) {
	Cout << "analytic MFMC." << std::endl;
	avg_eval_ratios  = avg_eval_ratios1;  avg_hf_target = avg_hf_target1;
	numSamples       = num_samp1;         avg_estvar    = avg_estvar1;
	avg_estvar_ratio = avg_estvar_ratio1;
      }
      else {
	Cout << "ensemble of two-model CVMC." << std::endl;
	avg_eval_ratios  = avg_eval_ratios2;  avg_hf_target = avg_hf_target2;
	numSamples       = num_samp2;         avg_estvar    = avg_estvar2;
	avg_estvar_ratio = avg_estvar_ratio2;
      }
    }
    else { // Run one numerical soln from best of two starting points
      if (budget_constrained) { // same cost, compare accuracy
	avg_estvar1 = acv_estimator_variance(avg_N_H, cost, avg_eval_ratios1,
					     avg_hf_target1);
	avg_estvar2 = acv_estimator_variance(avg_N_H, cost, avg_eval_ratios2,
					     avg_hf_target2);
	mfmc_init = (avg_estvar1 <= avg_estvar2);
      }
      else { // same accuracy (convergenceTol * estVarIter0), compare cost 
	avg_hf_target1 = update_hf_target(avg_eval_ratios1, varH, estVarIter0);
	avg_hf_target2 = update_hf_target(avg_eval_ratios2, varH, estVarIter0);
	mfmc_init
	  = (compute_equivalent_cost(avg_hf_target1, avg_eval_ratios1, cost)
	  <= compute_equivalent_cost(avg_hf_target2, avg_eval_ratios2, cost));
      }
      if (mfmc_init)
	{ avg_eval_ratios = avg_eval_ratios1; avg_hf_target = avg_hf_target1; }
      else
	{ avg_eval_ratios = avg_eval_ratios2; avg_hf_target = avg_hf_target2; }
      if (outputLevel >= NORMAL_OUTPUT) {
	Cout << "ACV initial guess candidates:\n  analytic MFMC estvar = "
	     << avg_estvar1 << "\n  ensemble CVMC estvar = " << avg_estvar2
	     << "\nACV initial guess from ";
	if (mfmc_init) Cout << "analytic MFMC ";
	else           Cout << "ensemble of two-model CVMC ";
	Cout << "(average eval ratios):\n" << avg_eval_ratios << std::endl;
      }
      // Single solve initiated from lowest estvar
      nonhierarch_numerical_solution(cost, approxSequence, avg_eval_ratios,
				     avg_hf_target, numSamples, avg_estvar,
				     avg_estvar_ratio);
    }
  }
  else { // update approx_sequence after shared sample increment
    //covariance_to_correlation_sq(covLH, var_L, varH, rho2LH);
    //RealVector avg_rho2_LH;  average(rho2LH, 0, avg_rho2_LH);
    //ordered_approx_sequence(avg_rho2_LH, approxSequence);

    // warm start from previous eval_ratios solution
    // > no scaling needed from prev soln (as in NonDLocalReliability) since
    //   updated avg_N_H now includes allocation from previous solution and is
    //   active on constraint bound (excepting integer sample rounding)
    approxSequence.clear();

    // Should not be required so long as previous solution is feasible:
    //Real avg_N_H = (backfillFailures) ? average(N_H_actual) : N_H_alloc;
    //Cout << "Before: avg_eval_ratios =:\n" << avg_eval_ratios
    // 	   << "avg_hf_target = " << avg_hf_target << std::endl;
    //scale_to_target(avg_N_H, cost, avg_eval_ratios, avg_hf_target);
    //Cout << "After:  avg_eval_ratios =:\n" << avg_eval_ratios
    // 	   << "avg_hf_target = " << avg_hf_target << std::endl;

    nonhierarch_numerical_solution(cost, approxSequence, avg_eval_ratios,
				   avg_hf_target, numSamples, avg_estvar,
				   avg_estvar_ratio);
  }

  if (outputLevel >= NORMAL_OUTPUT) {
    for (size_t approx=0; approx<numApprox; ++approx)
      Cout << "Approx " << approx+1 << ": average evaluation ratio = "
	   << avg_eval_ratios[approx] << '\n';
    Cout << "Average estimator variance = " << avg_estvar
	 << "\nAverage ACV variance / average MC variance = "
	 << avg_estvar_ratio << std::endl;
  }
}


Real NonDACVSampling::
update_hf_target(const RealVector& avg_eval_ratios, const RealVector& var_H,
		 const RealVector& estvar0)
{
  // Note: there is a circular dependency between estvar_ratios and hf_targets
  RealSymMatrix F;           compute_F_matrix(avg_eval_ratios, F);
  RealVector estvar_ratios;  acv_estvar_ratios(F, estvar_ratios);
  RealVector hf_targets(numFunctions, false);
  for (size_t qoi=0; qoi<numFunctions; ++qoi)
    hf_targets[qoi] = var_H[qoi] * estvar_ratios[qoi]
                    / (convergenceTol * estvar0[qoi]);
  Real avg_hf_target = average(hf_targets);
  return avg_hf_target;
}


/** Multi-moment map-based version used by ACV following shared_increment() */
void NonDACVSampling::
accumulate_acv_sums(IntRealMatrixMap& sum_L_baseline, IntRealVectorMap& sum_H,
		    IntRealSymMatrixArrayMap& sum_LL, // L w/ itself + other L
		    IntRealMatrixMap&         sum_LH, // each L with H
		    RealVector& sum_HH, SizetArray& N_shared)
{
  // uses one set of allResponses with QoI aggregation across all Models,
  // ordered by unorderedModels[i-1], i=1:numApprox --> truthModel

  using std::isfinite;  bool all_finite;
  Real lf_fn, lf2_fn, hf_fn, lf_prod, lf2_prod, hf_prod;
  IntRespMCIter r_it;              IntRVMIter    h_it;
  IntRMMIter lb_it, lr_it, lh_it;  IntRSMAMIter ll_it;
  int lb_ord, lr_ord, h_ord, ll_ord, lh_ord, active_ord, m;
  size_t qoi, approx, approx2, lf_index, lf2_index, hf_index;

  for (r_it=allResponses.begin(); r_it!=allResponses.end(); ++r_it) {
    const Response&   resp    = r_it->second;
    const RealVector& fn_vals = resp.function_values();
    //const ShortArray& asv   = resp.active_set_request_vector();

    for (qoi=0; qoi<numFunctions; ++qoi) {

      // see fault tol notes in NonDNonHierarchSampling::compute_correlation()
      all_finite = true;
      for (approx=0; approx<=numApprox; ++approx)
	if (!isfinite(fn_vals[approx * numFunctions + qoi])) // NaN or +/-Inf
	  { all_finite = false; break; }

      if (all_finite) {
	++N_shared[qoi];
	// High accumulations:
	hf_index = numApprox * numFunctions + qoi;
	hf_fn = fn_vals[hf_index];
	// High-High:
	sum_HH[qoi] += hf_fn * hf_fn; // a single vector for ord 1
	// High:
	h_it = sum_H.begin();  h_ord = (h_it == sum_H.end()) ? 0 : h_it->first;
	hf_prod = hf_fn;       active_ord = 1;
	while (h_ord) {
	  if (h_ord == active_ord) { // support general key sequence
	    h_it->second[qoi] += hf_prod;
	    ++h_it; h_ord = (h_it == sum_H.end()) ? 0 : h_it->first;
	  }
	  hf_prod *= hf_fn;  ++active_ord;
	}

	for (approx=0; approx<numApprox; ++approx) {
	  // Low accumulations:
	  lf_index = approx * numFunctions + qoi;
	  lf_fn = fn_vals[lf_index];

	  lb_it = sum_L_baseline.begin();
	  ll_it = sum_LL.begin();  lh_it = sum_LH.begin();
	  lb_ord = (lb_it == sum_L_baseline.end()) ? 0 : lb_it->first;
	  ll_ord = (ll_it == sum_LL.end())         ? 0 : ll_it->first;
	  lh_ord = (lh_it == sum_LH.end())         ? 0 : lh_it->first;
	  lf_prod = lf_fn;  hf_prod = hf_fn;  active_ord = 1;
	  while (lb_ord || ll_ord || lh_ord) {
    
	    // Low baseline
	    if (lb_ord == active_ord) { // support general key sequence
	      lb_it->second(qoi,approx) += lf_prod;  ++lb_it;
	      lb_ord = (lb_it == sum_L_baseline.end()) ? 0 : lb_it->first;
	    }
	    // Low-Low
	    if (ll_ord == active_ord) { // support general key sequence
	      RealSymMatrix& sum_LL_q = ll_it->second[qoi];
	      sum_LL_q(approx,approx) += lf_prod * lf_prod;
	      // Off-diagonal of C matrix:
	      // look back (only) for single capture of each combination
	      for (approx2=0; approx2<approx; ++approx2) {
		lf2_index = approx2 * numFunctions + qoi;
		lf2_prod = lf2_fn = fn_vals[lf2_index];
		for (m=1; m<active_ord; ++m)
		  lf2_prod *= lf2_fn;
		sum_LL_q(approx,approx2) += lf_prod * lf2_prod;
	      }
	      ++ll_it; ll_ord = (ll_it == sum_LL.end()) ? 0 : ll_it->first;
	    }
	    // Low-High (c vector for each QoI):
	    if (lh_ord == active_ord) {
	      lh_it->second(qoi,approx) += lf_prod * hf_prod;
	      ++lh_it; lh_ord = (lh_it == sum_LH.end()) ? 0 : lh_it->first;
	    }

	    lf_prod *= lf_fn;  hf_prod *= hf_fn;  ++active_ord;
	  }
	}
      }
    }
  }
}


/** Single moment version used by offline-pilot and pilot-projection ACV
    following shared_increment() */
void NonDACVSampling::
accumulate_acv_sums(RealMatrix& sum_L_baseline, RealVector& sum_H,
		    RealSymMatrixArray& sum_LL, // L w/ itself + other L
		    RealMatrix&         sum_LH, // each L with H
		    RealVector& sum_HH, SizetArray& N_shared)
{
  // uses one set of allResponses with QoI aggregation across all Models,
  // ordered by unorderedModels[i-1], i=1:numApprox --> truthModel

  using std::isfinite;  bool all_finite;
  Real lf_fn, lf2_fn, hf_fn;
  IntRespMCIter r_it;
  size_t qoi, approx, approx2, lf_index, lf2_index, hf_index;

  for (r_it=allResponses.begin(); r_it!=allResponses.end(); ++r_it) {
    const Response&   resp    = r_it->second;
    const RealVector& fn_vals = resp.function_values();
    //const ShortArray& asv   = resp.active_set_request_vector();

    for (qoi=0; qoi<numFunctions; ++qoi) {

      // see fault tol notes in NonDNonHierarchSampling::compute_correlation()
      all_finite = true;
      for (approx=0; approx<=numApprox; ++approx)
	if (!isfinite(fn_vals[approx * numFunctions + qoi])) // NaN or +/-Inf
	  { all_finite = false; break; }

      if (all_finite) {
	++N_shared[qoi];
	// High accumulations:
	hf_index = numApprox * numFunctions + qoi;
	hf_fn = fn_vals[hf_index];
	sum_H[qoi]  += hf_fn;         // High
	sum_HH[qoi] += hf_fn * hf_fn; // High-High

	RealSymMatrix& sum_LL_q = sum_LL[qoi];
	for (approx=0; approx<numApprox; ++approx) {
	  lf_index = approx * numFunctions + qoi;
	  lf_fn = fn_vals[lf_index];

	  // Low accumulations:
	  sum_L_baseline(qoi,approx) += lf_fn; // Low
	  sum_LL_q(approx,approx)    += lf_fn * lf_fn; // Low-Low
	  // Off-diagonal of C matrix:
	  // look back (only) for single capture of each combination
	  for (approx2=0; approx2<approx; ++approx2) {
	    lf2_index = approx2 * numFunctions + qoi;
	    lf2_fn = fn_vals[lf2_index];
	    sum_LL_q(approx,approx2) += lf_fn * lf2_fn;
	  }
	  // Low-High (c vector)
	  sum_LH(qoi,approx) += lf_fn * hf_fn;
	}
      }
    }
  }
}


/** Multi-moment map-based version with fine-grained fault tolerance, 
    used by ACV following shared_increment()
void NonDACVSampling::
accumulate_acv_sums(IntRealMatrixMap& sum_L_baseline, IntRealVectorMap& sum_H,
		    IntRealSymMatrixArrayMap& sum_LL, // L w/ itself + other L
		    IntRealMatrixMap&         sum_LH, // each L with H
		    RealVector& sum_HH, Sizet2DArray& num_L_baseline,
		    SizetArray& num_H,  SizetSymMatrixArray& num_LL,
		    Sizet2DArray& num_LH)
{
  // uses one set of allResponses with QoI aggregation across all Models,
  // ordered by unorderedModels[i-1], i=1:numApprox --> truthModel

  using std::isfinite;
  Real lf_fn, lf2_fn, hf_fn, lf_prod, lf2_prod, hf_prod;
  IntRespMCIter r_it;              IntRVMIter    h_it;
  IntRMMIter lb_it, lr_it, lh_it;  IntRSMAMIter ll_it;
  int lb_ord, lr_ord, h_ord, ll_ord, lh_ord, active_ord, m;
  size_t qoi, approx, approx2, lf_index, lf2_index, hf_index;
  bool hf_is_finite;

  for (r_it=allResponses.begin(); r_it!=allResponses.end(); ++r_it) {
    const Response&   resp    = r_it->second;
    const RealVector& fn_vals = resp.function_values();
    //const ShortArray& asv   = resp.active_set_request_vector();
    hf_index = numApprox * numFunctions;

    for (qoi=0; qoi<numFunctions; ++qoi, ++hf_index) {
      hf_fn = fn_vals[hf_index];
      hf_is_finite = isfinite(hf_fn);
      // High accumulations:
      if (hf_is_finite) { // neither NaN nor +/-Inf
	++num_H[qoi];
	// High-High:
	sum_HH[qoi] += hf_fn * hf_fn; // a single vector for ord 1
	// High:
	h_it = sum_H.begin();  h_ord = (h_it == sum_H.end()) ? 0 : h_it->first;
	hf_prod = hf_fn;       active_ord = 1;
	while (h_ord) {
	  if (h_ord == active_ord) { // support general key sequence
	    h_it->second[qoi] += hf_prod;
	    ++h_it; h_ord = (h_it == sum_H.end()) ? 0 : h_it->first;
	  }
	  hf_prod *= hf_fn;  ++active_ord;
	}
      }
	
      SizetSymMatrix& num_LL_q = num_LL[qoi];
      for (approx=0; approx<numApprox; ++approx) {
	lf_index = approx * numFunctions + qoi;
	lf_fn = fn_vals[lf_index];

	// Low accumulations:
	if (isfinite(lf_fn)) {
	  ++num_L_baseline[approx][qoi];
	  ++num_LL_q(approx,approx); // Diagonal of C matrix
	  if (hf_is_finite) ++num_LH[approx][qoi]; // pull out of moment loop

	  lb_it = sum_L_baseline.begin();
	  ll_it = sum_LL.begin();  lh_it = sum_LH.begin();
	  lb_ord = (lb_it == sum_L_baseline.end()) ? 0 : lb_it->first;
	  ll_ord = (ll_it == sum_LL.end())         ? 0 : ll_it->first;
	  lh_ord = (lh_it == sum_LH.end())         ? 0 : lh_it->first;
	  lf_prod = lf_fn;  active_ord = 1;
	  while (lb_ord || ll_ord || lh_ord) {
    
	    // Low baseline
	    if (lb_ord == active_ord) { // support general key sequence
	      lb_it->second(qoi,approx) += lf_prod;  ++lb_it;
	      lb_ord = (lb_it == sum_L_baseline.end()) ? 0 : lb_it->first;
	    }
	    // Low-Low
	    if (ll_ord == active_ord) { // support general key sequence
	      ll_it->second[qoi](approx,approx) += lf_prod * lf_prod;
	      // Off-diagonal of C matrix:
	      // look back (only) for single capture of each combination
	      for (approx2=0; approx2<approx; ++approx2) {
		lf2_index = approx2 * numFunctions + qoi;
		lf2_fn = fn_vals[lf2_index];

		if (isfinite(lf2_fn)) { // both are finite
		  if (active_ord == 1) ++num_LL_q(approx,approx2);
		  lf2_prod = lf2_fn;
		  for (m=1; m<active_ord; ++m)
		    lf2_prod *= lf2_fn;
		  ll_it->second[qoi](approx,approx2) += lf_prod * lf2_prod;
		}
	      }
	      ++ll_it; ll_ord = (ll_it == sum_LL.end()) ? 0 : ll_it->first;
	    }
	    // Low-High (c vector for each QoI):
	    if (lh_ord == active_ord) {
	      if (hf_is_finite) {
		hf_prod = hf_fn;
		for (m=1; m<active_ord; ++m)
		  hf_prod *= hf_fn;
		lh_it->second(qoi,approx) += lf_prod * hf_prod;
	      }
	      ++lh_it; lh_ord = (lh_it == sum_LH.end()) ? 0 : lh_it->first;
	    }

	    lf_prod *= lf_fn;  ++active_ord;
	  }
	}
      }
    }
  }
}


// Single moment version with fine-grained fault tolerance, used by 
// offline-pilot and pilot-projection ACV following shared_increment()
void NonDACVSampling::
accumulate_acv_sums(RealMatrix& sum_L_baseline, RealVector& sum_H,
		    RealSymMatrixArray& sum_LL, // L w/ itself + other L
		    RealMatrix&         sum_LH, // each L with H
		    RealVector& sum_HH, Sizet2DArray& num_L_baseline,
		    SizetArray& num_H,  SizetSymMatrixArray& num_LL,
		    Sizet2DArray& num_LH)
{
  // uses one set of allResponses with QoI aggregation across all Models,
  // ordered by unorderedModels[i-1], i=1:numApprox --> truthModel

  using std::isfinite;
  Real lf_fn, lf2_fn, hf_fn;
  IntRespMCIter r_it;
  size_t qoi, approx, approx2, lf_index, lf2_index, hf_index;
  bool hf_is_finite;

  for (r_it=allResponses.begin(); r_it!=allResponses.end(); ++r_it) {
    const Response&   resp    = r_it->second;
    const RealVector& fn_vals = resp.function_values();
    //const ShortArray& asv   = resp.active_set_request_vector();
    hf_index = numApprox * numFunctions;

    for (qoi=0; qoi<numFunctions; ++qoi, ++hf_index) {
      hf_fn = fn_vals[hf_index];
      hf_is_finite = isfinite(hf_fn);
      // High accumulations:
      if (hf_is_finite) { // neither NaN nor +/-Inf
	++num_H[qoi];
	sum_H[qoi]  += hf_fn;         // High
	sum_HH[qoi] += hf_fn * hf_fn; // High-High
      }
	
      SizetSymMatrix& num_LL_q = num_LL[qoi];
      RealSymMatrix&  sum_LL_q = sum_LL[qoi];
      for (approx=0; approx<numApprox; ++approx) {
	lf_index = approx * numFunctions + qoi;
	lf_fn = fn_vals[lf_index];

	// Low accumulations:
	if (isfinite(lf_fn)) {
	  ++num_L_baseline[approx][qoi];
	  sum_L_baseline(qoi,approx) += lf_fn; // Low

	  ++num_LL_q(approx,approx); // Diagonal of C matrix
	  sum_LL_q(approx,approx) += lf_fn * lf_fn; // Low-Low
	  // Off-diagonal of C matrix:
	  // look back (only) for single capture of each combination
	  for (approx2=0; approx2<approx; ++approx2) {
	    lf2_index = approx2 * numFunctions + qoi;
	    lf2_fn = fn_vals[lf2_index];
	    if (isfinite(lf2_fn)) { // both are finite
	       ++num_LL_q(approx,approx2);
	       sum_LL_q(approx,approx2) += lf_fn * lf2_fn;
	    }
	  }

	  if (hf_is_finite) {
	    ++num_LH[approx][qoi];
	    sum_LH(qoi,approx) += lf_fn * hf_fn;// Low-High (c vector)	    
	  }
	}
      }
    }
  }
}
*/


/** This version used by ACV following shared_approx_increment() */
void NonDACVSampling::
accumulate_acv_sums(IntRealMatrixMap& sum_L_shared,
		    IntRealSymMatrixArrayMap& sum_LL, // L w/ itself + other L
		    Sizet2DArray& N_L_shared)
{
  // uses one set of allResponses with QoI aggregation across all approx Models,
  // corresponding to unorderedModels[i-1], i=1:numApprox (omits truthModel)

  using std::isfinite;  bool all_lf_finite;
  Real lf_fn, lf2_fn, lf_prod, lf2_prod;
  IntRespMCIter r_it; IntRMMIter ls_it; IntRSMAMIter ll_it;
  int ls_ord, ll_ord, active_ord, m;
  size_t qoi, approx, approx2, lf_index, lf2_index;

  for (r_it=allResponses.begin(); r_it!=allResponses.end(); ++r_it) {
    const Response&   resp    = r_it->second;
    const RealVector& fn_vals = resp.function_values();
    //const ShortArray& asv   = resp.active_set_request_vector();

    for (qoi=0; qoi<numFunctions; ++qoi) {

      // see fault tol notes in NonDNonHierarchSampling::compute_correlation()
      all_lf_finite = true;
      for (approx=0; approx<numApprox; ++approx)
	if (!isfinite(fn_vals[approx * numFunctions + qoi])) // NaN or +/-Inf
	  { all_lf_finite = false; break; }

      if (all_lf_finite) {
	++N_L_shared[approx][qoi];

	for (approx=0; approx<numApprox; ++approx) {
	  // Low accumulations:
	  lf_index = approx * numFunctions + qoi;
	  lf_fn = fn_vals[lf_index];

	  ls_it = sum_L_shared.begin();  ll_it = sum_LL.begin();
	  ls_ord = (ls_it == sum_L_shared.end()) ? 0 : ls_it->first;
	  ll_ord = (ll_it == sum_LL.end())       ? 0 : ll_it->first;
	  lf_prod = lf_fn;  active_ord = 1;
	  while (ls_ord || ll_ord) {
    
	    // Low shared
	    if (ls_ord == active_ord) { // support general key sequence
	      ls_it->second(qoi,approx) += lf_prod;  ++ls_it;
	      ls_ord = (ls_it == sum_L_shared.end()) ? 0 : ls_it->first;
	    }
	    // Low-Low
	    if (ll_ord == active_ord) { // support general key sequence
	      RealSymMatrix& sum_LL_q = ll_it->second[qoi];
	      sum_LL_q(approx,approx) += lf_prod * lf_prod;
	      // Off-diagonal of C matrix:
	      // look back (only) for single capture of each combination
	      for (approx2=0; approx2<approx; ++approx2) {
		lf2_index = approx2 * numFunctions + qoi;
		lf2_prod = lf2_fn = fn_vals[lf2_index];
		for (m=1; m<active_ord; ++m)
		  lf2_prod *= lf2_fn;
		sum_LL_q(approx,approx2) += lf_prod * lf2_prod;
	      }
	      ++ll_it;  ll_ord = (ll_it == sum_LL.end()) ? 0 : ll_it->first;
	    }

	    lf_prod *= lf_fn;  ++active_ord;
	  }
	}
      }
    }
  }
}


/** This version used by ACV following approx_increment() */
void NonDACVSampling::
accumulate_acv_sums(IntRealMatrixMap& sum_L_refined,
		    Sizet2DArray& N_L_refined,
		    const SizetArray& approx_sequence,
		    size_t sequence_start, size_t sequence_end)
{
  // uses one set of allResponses with QoI aggregation across all Models,
  // led by the approx Model responses of interest

  using std::isfinite;
  int lr_ord, active_ord;  size_t s, qoi, lf_index, approx;
  Real lf_fn, lf_prod;  IntRespMCIter r_it;  IntRMMIter lr_it;
  bool ordered = approx_sequence.empty();

  for (r_it=allResponses.begin(); r_it!=allResponses.end(); ++r_it) {
    const Response&   resp    = r_it->second;
    const RealVector& fn_vals = resp.function_values();
    //const ShortArray& asv   = resp.active_set_request_vector();

    for (qoi=0; qoi<numFunctions; ++qoi) {

      for (s=sequence_start; s<sequence_end; ++s) {
	approx = (ordered) ? s : approx_sequence[s];
	lf_index = approx * numFunctions + qoi;
	lf_fn = fn_vals[lf_index];

	// Low accumulations:
	if (isfinite(lf_fn)) {
	  ++N_L_refined[approx][qoi];
	  lr_it = sum_L_refined.begin();
	  lr_ord = (lr_it == sum_L_refined.end()) ? 0 : lr_it->first;
	  lf_prod = lf_fn;  active_ord = 1;
	  while (lr_ord) {
    
	    // Low refined
	    if (lr_ord == active_ord) { // support general key sequence
	      lr_it->second(qoi,approx) += lf_prod;  ++lr_it;
	      lr_ord = (lr_it == sum_L_refined.end()) ? 0 : lr_it->first;
	    }

	    lf_prod *= lf_fn;  ++active_ord;
	  }
	}
      }
    }
  }
}


void NonDACVSampling::
compute_LH_covariance(const RealMatrix& sum_L_shared, const RealVector& sum_H,
		      const RealMatrix& sum_LH, const SizetArray& N_shared,
		      RealMatrix& cov_LH)
{
  if (cov_LH.empty()) cov_LH.shapeUninitialized(numFunctions, numApprox);

  size_t approx, qoi;
  for (approx=0; approx<numApprox; ++approx) {
    const Real* sum_L_shared_a = sum_L_shared[approx];
    const Real*       sum_LH_a =       sum_LH[approx];
    Real*             cov_LH_a =       cov_LH[approx];
    for (qoi=0; qoi<numFunctions; ++qoi)
      compute_covariance(sum_L_shared_a[qoi], sum_H[qoi], sum_LH_a[qoi],
			 N_shared[qoi], cov_LH_a[qoi]);
  }

  if (outputLevel >= DEBUG_OUTPUT)
    Cout << "cov_LH in compute_LH_covariance():\n" << cov_LH << std::endl;
}


void NonDACVSampling::
compute_LL_covariance(const RealMatrix& sum_L_shared,
		      const RealSymMatrixArray& sum_LL,
		      const SizetArray& N_shared,//SizetSymMatrixArray& N_LL,
		      RealSymMatrixArray& cov_LL)
{
  size_t qoi, approx, approx2;
  if (cov_LL.empty()) {
    cov_LL.resize(numFunctions);
    for (qoi=0; qoi<numFunctions; ++qoi)
      cov_LL[qoi].shapeUninitialized(numApprox);
  }

  Real sum_L_aq;  size_t N_sh_q;
  for (qoi=0; qoi<numFunctions; ++qoi) {
    const RealSymMatrix& sum_LL_q = sum_LL[qoi];
    RealSymMatrix&       cov_LL_q = cov_LL[qoi];
    N_sh_q = N_shared[qoi]; //const SizetSymMatrix&  N_LL_q = N_LL[qoi];
    for (approx=0; approx<numApprox; ++approx) {
      sum_L_aq = sum_L_shared(qoi,approx);
      for (approx2=0; approx2<=approx; ++approx2)
	compute_covariance(sum_L_aq, sum_L_shared(qoi,approx2),
			   sum_LL_q(approx,approx2), N_sh_q,
			   cov_LL_q(approx,approx2));
    }
  }

  if (outputLevel >= DEBUG_OUTPUT)
    Cout << "cov_LL in compute_LL_covariance():\n" << cov_LL << std::endl;
}


void NonDACVSampling::
acv_raw_moments(IntRealMatrixMap& sum_L_baseline,
		IntRealMatrixMap& sum_L_refined,   IntRealVectorMap& sum_H,
		IntRealSymMatrixArrayMap& sum_LL,  IntRealMatrixMap& sum_LH,
		const RealVector& avg_eval_ratios, const SizetArray& N_shared,
		const Sizet2DArray& N_L_refined,   RealMatrix& H_raw_mom)
{
  if (H_raw_mom.empty()) H_raw_mom.shapeUninitialized(numFunctions, 4);

  RealSymMatrix F, CF_inv;
  compute_F_matrix(avg_eval_ratios, F);

  size_t approx, qoi, N_shared_q;  Real sum_H_mq;
  RealVector beta(numApprox);
  for (int mom=1; mom<=4; ++mom) {
    RealMatrix&     sum_L_base_m = sum_L_baseline[mom];
    RealMatrix&      sum_L_ref_m = sum_L_refined[mom];
    RealVector&          sum_H_m =         sum_H[mom];
    RealSymMatrixArray& sum_LL_m =        sum_LL[mom];
    RealMatrix&         sum_LH_m =        sum_LH[mom];
    if (outputLevel >= NORMAL_OUTPUT)
      Cout << "Moment " << mom << " estimator:\n";
    for (qoi=0; qoi<numFunctions; ++qoi) {
      sum_H_mq = sum_H_m[qoi];  N_shared_q = N_shared[qoi];
      if (mom == 1) // variances/covariances already computed for mean estimator
	compute_acv_control(covLL[qoi], F, covLH, qoi, beta);
      else // compute variances/covariances for higher-order moment estimators
	compute_acv_control(sum_L_base_m, sum_H_mq, sum_LL_m[qoi], sum_LH_m,
			    N_shared_q, F, qoi, beta); // all use shared counts
        // *** TO DO3: support shared_approx_increment() --> baselineL

      Real& H_raw_mq = H_raw_mom(qoi, mom-1);
      H_raw_mq = sum_H_mq / N_shared_q; // first term to be augmented
      for (approx=0; approx<numApprox; ++approx) {
	if (outputLevel >= NORMAL_OUTPUT)
	  Cout << "   QoI " << qoi+1 << " Approx " << approx+1 << ": control "
	       << "variate beta = " << std::setw(9) << beta[approx] << '\n';
	// For ACV, shared counts are fixed at N_H for all approx
	apply_control(sum_L_base_m(qoi,approx), N_shared_q,
		      sum_L_ref_m(qoi,approx),  N_L_refined[approx][qoi],
		      beta[approx], H_raw_mq);
      }
    }
  }
  if (outputLevel >= NORMAL_OUTPUT) Cout << std::endl;
}


/** LF only */
void NonDACVSampling::
update_projected_lf_samples(Real avg_hf_target,
			    const RealVector& avg_eval_ratios,
			    const SizetArray& N_H_actual, size_t& N_H_alloc,
			    //SizetArray& delta_N_L_actual,
			    Real& delta_equiv_hf)
{
  Sizet2DArray N_L_actual;  inflate(N_H_actual, N_L_actual);
  SizetArray   N_L_alloc;   inflate(N_H_alloc,  N_L_alloc);
  size_t approx, alloc_incr, actual_incr;  Real lf_target;
  for (approx=0; approx<numApprox; ++approx) {
    lf_target = avg_eval_ratios[approx] * avg_hf_target;
    const SizetArray& N_L_actual_a = N_L_actual[approx];
    size_t&           N_L_alloc_a  = N_L_alloc[approx];
    alloc_incr  = one_sided_delta(N_L_alloc_a, lf_target);
    actual_incr = (backfillFailures) ?
      one_sided_delta(average(N_L_actual_a), lf_target) : alloc_incr;
    /*delta_N_L_actual[approx] += actual_incr;*/  N_L_alloc_a += alloc_incr;
    increment_equivalent_cost(actual_incr, sequenceCost, approx,
			      delta_equiv_hf);
  }

  finalize_counts(N_L_actual, N_L_alloc);
}


/** LF and HF */
void NonDACVSampling::
update_projected_samples(Real avg_hf_target, const RealVector& avg_eval_ratios,
			 const SizetArray& N_H_actual, size_t& N_H_alloc,
			 size_t& delta_N_H_actual,
			 /*SizetArray& delta_N_L_actual,*/ Real& delta_equiv_hf)
{
  // The N_L baseline is the shared set PRIOR to delta_N_H --> important for
  // cost incr even if lf_targets is defined robustly (hf_targets * eval_ratios)
  update_projected_lf_samples(avg_hf_target, avg_eval_ratios, N_H_actual,
			      N_H_alloc, /*delta_N_L_actual,*/ delta_equiv_hf);

  size_t alloc_incr = one_sided_delta(N_H_alloc, avg_hf_target),
    actual_incr = (backfillFailures) ?
      one_sided_delta(average(N_H_actual), avg_hf_target) : alloc_incr;
  delta_N_H_actual += actual_incr;  N_H_alloc += alloc_incr;
  increment_equivalent_cost(actual_incr, sequenceCost, numApprox,
			    delta_equiv_hf);
}

} // namespace Dakota
