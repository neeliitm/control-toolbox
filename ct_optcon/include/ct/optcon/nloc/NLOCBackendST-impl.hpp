/***********************************************************************************
Copyright (c) 2017, Michael Neunert, Markus Giftthaler, Markus Stäuble, Diego Pardo,
Farbod Farshidian. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of ETH ZURICH nor the names of its contributors may be used
      to endorse or promote products derived from this software without specific
      prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
SHALL ETH ZURICH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************************************************************************************/

#pragma once

namespace ct{
namespace optcon{


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
NLOCBackendST<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::NLOCBackendST(const OptConProblem<STATE_DIM, CONTROL_DIM, SCALAR>& optConProblem,
		const NLOptConSettings& settings) :
			Base(optConProblem, settings)
{}


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
NLOCBackendST<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::NLOCBackendST(const OptConProblem<STATE_DIM, CONTROL_DIM, SCALAR>& optConProblem,
		 const std::string& settingsFile,
		 bool verbose,
		 const std::string& ns) :
		Base(optConProblem, settingsFile, verbose, ns)
{}


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
NLOCBackendST<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::~NLOCBackendST() {};


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
void NLOCBackendST<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::computeLinearizedDynamicsAroundTrajectory(size_t firstIndex, size_t lastIndex)
{
	for (size_t k=firstIndex; k <= lastIndex; k++)
	{
		this->computeLinearizedDynamics(this->settings_.nThreads, k);
	}
}


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
void NLOCBackendST<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::computeQuadraticCostsAroundTrajectory(size_t firstIndex, size_t lastIndex)
{
	if(lastIndex == this->K_-1)
		this->initializeCostToGo();

	for (size_t k=firstIndex; k<=lastIndex; k++)
	{
		// compute quadratic cost
		this->computeQuadraticCosts(this->settings_.nThreads, k);
	}
}


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
void NLOCBackendST<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::rolloutShots(size_t firstIndex, size_t lastIndex)
{
	for (size_t k=firstIndex; k<=lastIndex; k = k+ this->settings_.K_shot)
	{
		// rollout the shot
		this->rolloutSingleShot(this->settings_.nThreads, k, this->u_ff_, this->x_, this->x_, this->xShot_, *this->substepsX_, *this->substepsU_);

		this->computeSingleDefect(k, this->x_, this->xShot_, this->lqocProblem_->b_);
	}
}


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
SCALAR NLOCBackendST<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::performLineSearch()
{
	// we start with extrapolation
	double alpha = this->settings_.lineSearchSettings.alpha_0;
	double alphaBest = 0.0;
	size_t iterations = 0;

	this->lx_norm_ = 0.0;
	this->lu_norm_ = 0.0;


	while (iterations < this->settings_.lineSearchSettings.maxIterations)
	{
		if(this->settings_.lineSearchSettings.debugPrint)
			std::cout<<"[LineSearch]: Iteration: "<< iterations << ", try alpha: "<<alpha<< " out of maximum " << this->settings_.lineSearchSettings.maxIterations << " iterations. "<< std::endl;

		iterations++;

		SCALAR cost = std::numeric_limits<SCALAR>::max();
		SCALAR intermediateCost = std::numeric_limits<SCALAR>::max();
		SCALAR finalCost = std::numeric_limits<SCALAR>::max();
		SCALAR defectNorm = std::numeric_limits<SCALAR>::max();

		ct::core::StateVectorArray<STATE_DIM, SCALAR> x_search(this->K_+1);
		ct::core::StateVectorArray<STATE_DIM, SCALAR> x_shot_search(this->K_+1);
		ct::core::StateVectorArray<STATE_DIM, SCALAR> defects_recorded(this->K_+1, ct::core::StateVector<STATE_DIM, SCALAR>::Zero());
		ct::core::ControlVectorArray<CONTROL_DIM, SCALAR> u_recorded(this->K_);

		typename Base::StateSubstepsPtr substepsX = typename Base::StateSubstepsPtr(new typename Base::StateSubsteps(this->K_+1));
		typename Base::ControlSubstepsPtr substepsU = typename Base::ControlSubstepsPtr(new typename Base::ControlSubsteps(this->K_+1));

		//! set init state
		x_search[0] = this->x_[0];



		switch(this->settings_.nlocp_algorithm)
		{
		case NLOptConSettings::NLOCP_ALGORITHM::GNMS :
		{
			this->executeLineSearchMultipleShooting(this->settings_.nThreads, alpha, this->lu_, this->lx_, x_search, x_shot_search, defects_recorded, u_recorded, intermediateCost, finalCost, defectNorm, *substepsX, *substepsU);

			break;
		}
		case NLOptConSettings::NLOCP_ALGORITHM::ILQR :
		{
			defectNorm = 0.0;
			this->executeLineSearchSingleShooting(this->settings_.nThreads, alpha, x_search, u_recorded, intermediateCost, finalCost, *substepsX, *substepsU);
			break;
		}
		default :
			throw std::runtime_error("Algorithm type unknown in performLineSearch()!");
		}


		cost = intermediateCost + finalCost + this->settings_.meritFunctionRho * defectNorm;

		//! catch the case that a rollout might be unstable
		if(std::isnan(cost) || cost >= this->lowestCost_ ) // todo: alternatively cost >= (this->lowestCost_*(1 - 1e-3*alpha)), study this
		{
			if(this->settings_.lineSearchSettings.debugPrint){
				std::cout<<"[LineSearch]: No better cost/merit found at alpha "<< alpha << ":" << std::endl;
				std::cout<<"[LineSearch]: Cost:\t"<<intermediateCost + finalCost<<std::endl;
				std::cout<<"[LineSearch]: Defect:\t"<<defectNorm<<std::endl;
				std::cout<<"[LineSearch]: Merit:\t"<<cost<<std::endl;
			}

			//! compute new alpha
			alpha = alpha * this->settings_.lineSearchSettings.n_alpha;
		}
		else
		{
			//! cost < this->lowestCost_ , better merit/cost found!

			if(this->settings_.lineSearchSettings.debugPrint){
				std::cout<<"Lower cost/merit found at alpha: "<< alpha << ":" << std::endl;
				std::cout<<"merit: " << cost << "cost "<<intermediateCost + finalCost<<", defect " << defectNorm << " at alpha: "<< alpha << std::endl;
			}


			if(this->settings_.printSummary)
			{
				this-> lu_norm_ = this->template computeDiscreteArrayNorm<ct::core::ControlVectorArray<CONTROL_DIM, SCALAR>, 2> (u_recorded, this->u_ff_prev_);
				this-> lx_norm_ = this->template computeDiscreteArrayNorm<ct::core::StateVectorArray<STATE_DIM, SCALAR>, 2>(x_search, this->x_prev_);
			}else
			{
#ifdef MATLAB
				this-> lu_norm_ = this->template computeDiscreteArrayNorm<ct::core::ControlVectorArray<CONTROL_DIM, SCALAR>, 2> (u_recorded, this->u_ff_prev_);
				this-> lx_norm_ = this->template computeDiscreteArrayNorm<ct::core::StateVectorArray<STATE_DIM, SCALAR>, 2>(x_search, this->x_prev_);
#endif
			}

			alphaBest = alpha;
			this->intermediateCostBest_ = intermediateCost;
			this->finalCostBest_ = finalCost;
			this->d_norm_ = defectNorm;
			this->x_prev_ = x_search;
			this->lowestCost_ = cost;
			this->x_.swap(x_search);
			this->xShot_.swap(x_shot_search);
			this->u_ff_.swap(u_recorded);
			this->lqocProblem_->b_.swap(defects_recorded);
			this->substepsX_ = substepsX;
			this->substepsU_ = substepsU;
			break;
		}
	} //! end while

	return alphaBest;
}


} // namespace optcon
} // namespace ct

