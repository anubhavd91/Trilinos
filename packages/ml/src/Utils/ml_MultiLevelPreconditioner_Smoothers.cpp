/*!
 *  \file ml_MultiLevelPreconditioner_Smoothers.cpp
 *
 *  \brief ML black-box preconditioner for Epetra_RowMatrix derived classes.
 *
 *  \author Marzio Sala, SNL, 9214
 *
 *  \date Last update do Doxygen: 22-Jul-04
 *
 */


#include "ml_common.h"
#include "ml_include.h"
#if defined(HAVE_ML_EPETRA) && defined(HAVE_ML_TEUCHOS)
#include "Epetra_Map.h"
#include "Epetra_Vector.h"
#include "Epetra_FECrsMatrix.h"
#include "Epetra_VbrMatrix.h"
#include "Epetra_SerialDenseMatrix.h"
#include "Epetra_SerialDenseVector.h"
#include "Epetra_SerialDenseSolver.h"
#include "Epetra_Import.h"
#include "Epetra_Time.h"
#include "Epetra_RowMatrix.h"
#ifdef ML_MPI
#include "Epetra_MpiComm.h"
#else
#include "Epetra_SerialComm.h"
#endif
#include "ml_ifpack_wrap.h"
#include "Teuchos_ParameterList.hpp"
#include "ml_epetra.h"
#include "ml_MultiLevelPreconditioner.h"

using namespace Teuchos;

// ============================================================================
/*! Values for \c "smoother: type"
 * - \c Jacobi
 * - \c Gauss-Seidel
 * - \c symmetric Gauss-Seidel
 * - \c block Gauss-Seidel
 * - \c MLS
 * - \c Aztec
 * - \c IFPACK
 * - \c do-nothing
 */
int ML_Epetra::MultiLevelPreconditioner::SetSmoothers() 
{

  char parameter[80];
  Epetra_Time Time(Comm());

  sprintf(parameter,"%ssmoother: sweeps", Prefix_.c_str());
  int num_smoother_steps = List_.get(parameter, 1);

  sprintf(parameter,"%ssmoother: damping factor", Prefix_.c_str());
  double omega = List_.get(parameter,1.0);

  sprintf(parameter,"%ssmoother: pre or post", Prefix_.c_str());
  int pre_or_post = 0;
  string PreOrPostSmoother = List_.get(parameter,"both");

  sprintf(parameter,"%ssmoother: type", Prefix_.c_str());
  string Smoother = List_.get(parameter,"Aztec");

  sprintf(parameter,"%ssmoother: Aztec options", Prefix_.c_str());
  int * SmootherOptionsPtr = NULL;
  SmootherOptionsPtr = List_.get(parameter,SmootherOptionsPtr);

  sprintf(parameter,"%ssmoother: Aztec params", Prefix_.c_str());
  double * SmootherParamsPtr = NULL;
  SmootherParamsPtr = List_.get(parameter,SmootherParamsPtr);

  sprintf(parameter,"%ssmoother: Aztec as solver", Prefix_.c_str());
  bool AztecSmootherAsASolver = List_.get(parameter,false);
  int aztec_its;

  sprintf(parameter,"%ssmoother: MLS polynomial order", Prefix_.c_str());
  int MLSPolynomialOrder = List_.get(parameter,3);

  sprintf(parameter,"%ssmoother: MLS alpha",Prefix_.c_str());
  double MLSalpha = List_.get(parameter,30.0);
  
  int SmootherLevels = (NumLevels_>1)?(NumLevels_-1):1;

  sprintf(parameter,"%ssmoother: ParaSails levels",Prefix_.c_str());
  int ParaSailsN = List_.get(parameter,0);

  // this can be:
  // 0) nonsymmetric and/or indefinite
  // 1) SPD
  // (The ParaSails manual seems to allow also 2, but not ML)
  sprintf(parameter,"%ssmoother: ParaSails matrix",Prefix_.c_str());
  int ParaSailsSym = List_.get(parameter,0);

  sprintf(parameter,"%ssmoother: ParaSails threshold",Prefix_.c_str());
  double ParaSailsThresh = List_.get(parameter,0.01);

  sprintf(parameter,"%ssmoother: ParaSails filter",Prefix_.c_str());
  double ParaSailsFilter = List_.get(parameter,0.05);

  sprintf(parameter,"%ssmoother: ParaSails load balancing",Prefix_.c_str());
  double ParaSailsLB = List_.get(parameter,0.0);

  sprintf(parameter,"%ssmoother: ParaSails factorized", Prefix_.c_str());
  int ParaSailsFactorized = List_.get(parameter,0);

  // ===================== //
  // cycle over all levels //
  // ===================== //
 
  for( int level=0 ; level<SmootherLevels ; ++level ) {

    if(  verbose_ ) cout << endl;

    Time.ResetStartTime();

    // general parameters for more than one smoother

    sprintf(parameter,"%ssmoother: sweeps (level %d)", Prefix_.c_str(), LevelID_[level] );
    num_smoother_steps = List_.get(parameter,num_smoother_steps);

    sprintf(parameter,"%ssmoother: damping factor (level %d)", Prefix_.c_str(), LevelID_[level] );
    omega = List_.get(parameter,omega);

    sprintf(parameter,"%ssmoother: pre or post (level %d)", Prefix_.c_str(), LevelID_[level] );
    PreOrPostSmoother = List_.get(parameter, PreOrPostSmoother);
    
    if( PreOrPostSmoother == "post" ) pre_or_post = ML_POSTSMOOTHER;
    else if( PreOrPostSmoother == "pre" ) pre_or_post = ML_PRESMOOTHER;
    else if( PreOrPostSmoother == "both" ) pre_or_post = ML_BOTH;
    else 
      cerr << ErrorMsg_ << "smoother not recognized (" << PreOrPostSmoother << ")\n";
    
    sprintf(parameter,"%ssmoother: type (level %d)", Prefix_.c_str(), LevelID_[level]);
    Smoother = List_.get(parameter,Smoother);

    char msg[80];
    sprintf(msg,"Smoother (level %d) : ", LevelID_[level]);

    if( Smoother == "Jacobi" ) {

      // ============ //
      // point Jacobi //
      // ============ //

      if( verbose_ ) cout << msg << "Jacobi (sweeps="
			 << num_smoother_steps << ",omega=" << omega << ","
			 << PreOrPostSmoother << ")" << endl;
      ML_Gen_Smoother_Jacobi(ml_, LevelID_[level], pre_or_post,
			     num_smoother_steps, omega);
     
    } else if( Smoother == "Gauss-Seidel" ) {

      // ================== //
      // point Gauss-Seidel //
      // ================== //

      if( verbose_ ) cout << msg << "Gauss-Seidel (sweeps="
			 << num_smoother_steps << ",omega=" << omega << ","
			 << PreOrPostSmoother << ")" << endl;
      ML_Gen_Smoother_GaussSeidel(ml_, LevelID_[level], pre_or_post,
				  num_smoother_steps, omega);

    } else if( Smoother == "symmetric Gauss-Seidel" ) {

      // ====================== //
      // symmetric Gauss-Seidel //
      // ====================== //

      if( verbose_ ) cout << msg << "symmetric Gauss-Seidel (sweeps="
			  << num_smoother_steps << ",omega=" << omega << ","
			  << PreOrPostSmoother << ")" << endl;
      ML_Gen_Smoother_SymGaussSeidel(ml_, LevelID_[level], pre_or_post,
				     num_smoother_steps, omega);
    } else if( Smoother == "block Gauss-Seidel" ) {

      // ================== //
      // block Gauss-Seidel //
      // ================== //
      
      if( verbose_ ) cout << msg << "block Gauss-Seidel (sweeps="
			  << num_smoother_steps << ",omega=" << omega << ","
			  << PreOrPostSmoother << ")" << endl;
      ML_Gen_Smoother_BlockGaussSeidel(ml_, LevelID_[level], pre_or_post,
				       num_smoother_steps, omega, NumPDEEqns_);
    } else if( Smoother == "MLS" ) {

      // === //
      // MLS //
      // === //

      sprintf(parameter,"smoother: MLS polynomial order (level %d)", LevelID_[level]);
      if( verbose_ ) cout << msg << "MLS,"
			 << PreOrPostSmoother << endl;
      
      ML_Gen_Smoother_MLS(ml_, LevelID_[level], pre_or_post, MLSalpha,
			  MLSPolynomialOrder);
      
    } else if( Smoother == "Aztec" ) {
      
#ifdef HAVE_ML_AZTECOO
      // ======= //
      // AztecOO //
      // ======= //
      
      sprintf(parameter,"%ssmoother: Aztec options (level %d)", Prefix_.c_str(), LevelID_[level]);
      SmootherOptionsPtr = List_.get(parameter, SmootherOptionsPtr);
      sprintf(parameter,"%ssmoother: Aztec params (level %d)", Prefix_.c_str(), LevelID_[level]);
      SmootherParamsPtr = List_.get(parameter, SmootherParamsPtr);
      sprintf(parameter,"%ssmoother: Aztec as solver (level %d)", Prefix_.c_str(), LevelID_[level]);
      AztecSmootherAsASolver = List_.get(parameter,AztecSmootherAsASolver);
     
      if( AztecSmootherAsASolver == false ) aztec_its = AZ_ONLY_PRECONDITIONER;
      else                                  aztec_its = num_smoother_steps;
      
      if( SmootherOptionsPtr == NULL || SmootherParamsPtr == NULL ) {
      	SmootherOptionsPtr = SmootherOptions_;
      	SmootherParamsPtr = SmootherParams_;
      }

      if( verbose_ ) {
	cout << msg << "Aztec";
	if( SmootherOptionsPtr[AZ_precond] == AZ_dom_decomp ) {
	  cout << " DD, overlap=" << SmootherOptionsPtr[AZ_overlap] << ", ";
	  if( SmootherOptionsPtr[AZ_reorder] == 1 ) cout << "reord, ";
	  else cout << "no reord, ";
	  switch( SmootherOptionsPtr[AZ_subdomain_solve] ) {
	  case AZ_lu: cout << " LU"; break;
	  case AZ_ilu:
	    cout << "ILU(fill="  << SmootherOptionsPtr[AZ_graph_fill] << ")";
	    break;
	  case AZ_ilut:
	    cout << "ILUT(fill=" << SmootherParamsPtr[AZ_ilut_fill] << ",drop="
		 << SmootherParamsPtr[AZ_drop] << ")";
	    break;
	  case AZ_icc:
	    cout << "ICC(fill="  << SmootherOptionsPtr[AZ_graph_fill] << ")";
	    break;
	  case AZ_bilu:
	    cout << "BILU(fill="  << SmootherOptionsPtr[AZ_graph_fill] << ")";
	    break;
	  case AZ_rilu:
	    cout << "RILU(fill="  << SmootherOptionsPtr[AZ_graph_fill] << ",omega="
		 << SmootherParamsPtr[AZ_omega] << ")";
	    break;
	  }
	} else if( SmootherOptionsPtr[AZ_precond] == AZ_Jacobi ) {
	  cout << msg << " Jacobi preconditioner";
	} else if( SmootherOptionsPtr[AZ_precond] == AZ_Neumann ) {
	  cout << msg << " Neumann preconditioner, order = " << SmootherOptionsPtr[AZ_poly_ord];
	} else if( SmootherOptionsPtr[AZ_precond] == AZ_ls ) {
	  cout << msg << " LS preconditioner, order = " << SmootherOptionsPtr[AZ_poly_ord];
	} else if( SmootherOptionsPtr[AZ_precond] == AZ_sym_GS ) {
	  cout << msg << " symmetric Gauss-Seidel preconditioner, sweeps = " << SmootherOptionsPtr[AZ_poly_ord];
	} else if( SmootherOptionsPtr[AZ_precond] == AZ_none ) {
	  cout << msg << " with no preconditioning";
	}
	cout << ", "  << PreOrPostSmoother << endl;
      }
      
      ML_Gen_SmootherAztec(ml_, LevelID_[level], SmootherOptionsPtr, SmootherParamsPtr,
			   ProcConfig_, SmootherStatus_,
			   aztec_its, pre_or_post, NULL);
      
#else
      cerr << "Please configure ML with --enable-aztecoo to use" << endl;
      cerr << "AztecOO smoothers" << endl;
      exit(EXIT_FAILURE);
#endif
    } else if( Smoother == "IFPACK" ) {

      // ====== //
      // IFPACK //
      // ====== //

#ifdef HAVE_ML_IFPACK
      sprintf(parameter,"%ssmoother: ifpack type", Prefix_.c_str());
      string IfpackType = List_.get(parameter,"Amesos");

      sprintf(parameter,"%ssmoother: ifpack overlap", Prefix_.c_str());
      int IfpackOverlap = List_.get(parameter,0);

      if( verbose_ ) {
	cout << msg << "IFPACK, type = `" << IfpackType << "', " << endl
	     << msg << PreOrPostSmoother
	     << ", Overlap = " << IfpackOverlap << endl;
      }

      sprintf(parameter,"%ssmoother: ifpack list", Prefix_.c_str());
      Teuchos::ParameterList& IfpackList = List_.sublist(parameter);
      ML_Gen_Smoother_Ifpack(ml_, IfpackType.c_str(),
                             IfpackOverlap, LevelID_[level], pre_or_post,
                             IfpackList,*Comm_);

#else
      cerr << ErrorMsg_ << "IFPACK not available." << endl
	   << ErrorMsg_ << "ML must be configure with --enable-ifpack" << endl
	   << ErrorMsg_ << "to use IFPACK as a smoother" << endl
	   << ErrorMsg_ << "NO SMOOTHER SET FOR THIS LEVEL" << endl;
#endif

    } else if( Smoother == "ParaSails" ) {

#ifdef HAVE_ML_PARASAILS
      // ========= //
      // ParaSails //
      // ========= //

      sprintf(parameter,"%ssmoother: ParaSails levels (level %d)",
	      Prefix_.c_str(), level);
      ParaSailsN = List_.get(parameter,ParaSailsN);

      sprintf(parameter,"%ssmoother: ParaSails matrix (level %d)",
	      Prefix_.c_str(), level);
      ParaSailsSym = List_.get(parameter,ParaSailsSym);

      sprintf(parameter,"%ssmoother: ParaSails threshold (level %d)",
	      Prefix_.c_str(), level);
      ParaSailsThresh = List_.get(parameter,ParaSailsThresh);

      sprintf(parameter,"%ssmoother: ParaSails filter (level %d)",
	      Prefix_.c_str(), level);
      ParaSailsFilter = List_.get(parameter,ParaSailsFilter);

      sprintf(parameter,"%ssmoother: ParaSails load balancing (level %d)",
	      Prefix_.c_str(), level);
      ParaSailsLB = List_.get(parameter,ParaSailsLB);

      sprintf(parameter,"%ssmoother: ParaSails factorized (level %d)",
	      Prefix_.c_str(), level);
      ParaSailsFactorized = List_.get(parameter,ParaSailsFactorized);

      if( verbose_ ) 
	cout << msg << "ParaSails "
	     << "(n=" << ParaSailsN
	     << ",sym=" << ParaSailsSym 
	     << ",thresh=" << ParaSailsThresh 
	     << ",filter=" << ParaSailsFilter 
	     << ",lb=" << ParaSailsLB
	     << ")" << endl;
      
      // I am not sure about the ending `0' and of ML
      ML_Gen_Smoother_ParaSails(ml_, LevelID_[level], 
				pre_or_post, num_smoother_steps,
				ParaSailsSym, ParaSailsThresh,
				ParaSailsN,
				ParaSailsFilter, (int)ParaSailsLB, 
				ParaSailsFactorized);
#else
      cerr << ErrorMsg_ << "ParaSails not available." << endl
	   << ErrorMsg_ << "ML must be configure with --with-ml_parasails" << endl
	   << ErrorMsg_ << "to use ParaSails as a smoother" << endl
	   << ErrorMsg_ << "NO SMOOTHER SET FOR THIS LEVEL" << endl;
#endif

    } else if( Smoother == "do-nothing" ) {

      // ======================== //
      // do-nothing (no smoother) //
      // ======================== //

      if( verbose_ ) cout << msg << "do-nothing smoother" << endl;

    } else {

      // ======================================= //
      // error: no smoother found with this name //
      // ======================================= //

      if (Comm().MyPID() == 0)
	cerr << ErrorMsg_ << "Smoother not recognized!" << endl
	     << ErrorMsg_ << "(file " << __FILE__ << ",line " << __LINE__ << ")" << endl
	     << ErrorMsg_ << "Now is: " << Smoother << ". It should be: " << endl
	     << ErrorMsg_ << "<Jacobi> / <Gauss-Seidel> / <block Gauss-Seidel>" << endl
	     << ErrorMsg_ << "<symmetric Gauss-Seidel> / <Aztec> / <IFPACK>" << endl
	     << ErrorMsg_ << "<MLS> / <ParaSails>" << endl;
      ML_EXIT(-99);
    }
    
    if( verbose_ ) 
      cout << msg << "Setup time : " << Time.ElapsedTime() << " (s)" << endl;
    
  } /* for */

  if(  verbose_ ) cout << endl;

  return(0);
}

// ============================================================================

/*! Options for Maxwell smoothing:
 *  - \c "smoother: sweeps" (int)
 *  - \c "smoother: type" (string). Possible values:
 *    - \c MLS
 *    - \c Gauss-Seidel
 *  - \c "smoother: node: sweeps" (int): sweeps for nodal hierarchy
 *  - \c "smoother: node: damping factor" (double)
 *  - \c "smoother: edge: sweeps" (int): sweeps for edge hierarchy
 *  - \c "smoother:  edge: damping factor" (double)
 *  
 * This function is still under development. The following options are
 * hardwired:
 * - HALF_HIPTMAIR
 *   
 */

int ML_Epetra::MultiLevelPreconditioner::SetSmoothersMaxwell()
{

  char parameter[80];
  
  sprintf(parameter,"%ssmoother: sweeps", Prefix_.c_str());
  int num_smoother_sweeps = List_.get(parameter, 1);

  sprintf(parameter,"%ssmoother: type", Prefix_.c_str());
  string SmootherType = List_.get(parameter,"MLS");

  // get user's defined parameters for nodal smoothing
 
  sprintf(parameter,"%ssmoother: node: sweeps", Prefix_.c_str());
  int nodal_its = List_.get(parameter, 1);

  sprintf(parameter,"%ssmoother: node: damping factor", Prefix_.c_str());
  double nodal_omega = List_.get(parameter,1.0);


  // get user's defined parameters for edge smoothing
 
  sprintf(parameter,"%ssmoother: edge: sweeps", Prefix_.c_str());
  int edge_its = List_.get(parameter, 1);

  sprintf(parameter,"%ssmoother: edge: damping factor", Prefix_.c_str());
  double edge_omega = List_.get(parameter,1.0);

  // arguments for nodal smoother

  nodal_args_ = ML_Smoother_Arglist_Create(2);

  int hiptmair_type = HALF_HIPTMAIR;

  int SmootherLevels = (NumLevels_>1)?(NumLevels_-1):1;
    
  // set the smoother (only two choices at this time)

  SmootherType = "Gauss-Seidel";

  if( SmootherType == "MLS" ) {
#ifdef LATER    
    int coarsest_level = -1;
    cout << "FIX COARSEST_LEVEL!!!" << endl;
    int Ncoarse_edge, Ncoarse_node;
    double edge_coarsening_rate, node_coarsening_rate;
    int Nfine_edge, itmp;

    for( int level=0 ; level<SmootherLevels ; ++level ) {

      if (level != coarsest_level) {
	Ncoarse_edge = Tmat_array[level-1]->outvec_leng;
	ML_gsum_scalar_int(&Ncoarse_edge, &itmp, ml_edges_->comm);
	edge_coarsening_rate =  2.*((double) Nfine_edge)/
                                   ((double) Ncoarse_edge);
      }
      else edge_coarsening_rate =  (double) Nfine_edge;

      ML_Smoother_Arglist_Set(edge_args_, 1, &edge_coarsening_rate);
      Nfine_edge = Ncoarse_edge;

      if (level != coarsest_level) {
	Ncoarse_node = Tmat_array[level-1]->invec_leng;
	ML_gsum_scalar_int(&Ncoarse_node, &itmp, ml_edges_->comm);
	node_coarsening_rate =  2.*((double) Nfine_node)/ 
                                   ((double) Ncoarse_node);
      }
      else node_coarsening_rate = (double) Nfine_node;

      ML_Smoother_Arglist_Set(nodal_args_, 1, &node_coarsening_rate);
      Nfine_node = Ncoarse_node;
    }

    ML_Gen_Smoother_Hiptmair(ml_edges_, LevelID_[level], ML_BOTH, num_smoother_sweeps,
			     Tmat_array, Tmat_trans_array, NULL, 
			     (void *)ML_Gen_Smoother_MLS, edge_args, 
			     (void *)ML_Gen_Smoother_MLS, nodal_args, hiptmair_type);
#endif
  } else if( SmootherType == "Gauss-Seidel") {

    ML_Smoother_Arglist_Set(nodal_args_, 0, &nodal_its);
    ML_Smoother_Arglist_Set(nodal_args_, 1, &nodal_omega);

    // arguments for edge smoother
    edge_args_ = ML_Smoother_Arglist_Create(2);

    ML_Smoother_Arglist_Set(edge_args_, 0, &edge_its);
    ML_Smoother_Arglist_Set(edge_args_, 1, &edge_omega);

    for( int level=0 ; level<SmootherLevels ; ++level ) {

      ML_Gen_Smoother_Hiptmair(ml_edges_, LevelID_[level], ML_BOTH, num_smoother_sweeps,
			       Tmat_array, Tmat_trans_array, NULL, 
			       (void *)ML_Gen_Smoother_SymGaussSeidel, edge_args_, 
			       (void *)ML_Gen_Smoother_SymGaussSeidel, nodal_args_, hiptmair_type);
    }
  } else {
    cerr << ErrorMsg_ << "`smoother: type' has an incorrect value (" << SmootherType << ")" << endl
         << ErrorMsg_ << "For Maxwell, it should be" << endl
	 << ErrorMsg_ << "<MLS> / <Gauss-Seidel>" << endl;
    exit( EXIT_FAILURE );
  }

  return(0);
}

#endif /*ifdef ML_WITH_EPETRA && ML_HAVE_TEUCHOS*/

