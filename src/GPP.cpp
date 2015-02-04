/*
 * Function for solving a geometric program
*/
#include "cccp3.h"
using namespace arma;

CPS* gpp(std::vector<mat> FList, std::vector<mat> gList, CONEC& cList, mat A, mat b, CTRL& ctrl){
  // Initializing objects
  int n = cList.n, ne = n - 1, m = sum(cList.dims), 
    mnl = cList.dims(0), sizeLHS = A.n_rows + A.n_cols - 1;
  // Constraints
  CONEC cEpi;
  // Primal dual variables
  PDV* pdv = cList.initpdv(A.n_rows);
  PDV* dpdv = cList.initpdv(A.n_rows);
  // Solution
  CPS* cps = new CPS();
  cps->set_pdv(*pdv);
  cps->set_sidx(cList.sidx);
  // Objects used in iterations
  Rcpp::NumericVector state = cps->get_state();
  bool checkRgap = false, backTrack;
  double gap = m, resx, resy, resz, pcost = 1.0, dcost = 1.0, rgap = NA_REAL, 
    pres = 1.0, dres = 1.0, pres0 = 1.0, dres0 = 1.0, sigma, mu, ts, tz, tm, step, a, x1;
  vec ss(3);
  mat H(ne, ne), rx, ry, rz(cList.G.n_rows, 1), Lambda, LambdaPrd, Ws3, x, 
    ux(ne, 1), uz, RHS(ne, 1);
  mat OneE = cList.sone();
  // Initialising LHS matrices
  mat LHS = zeros(sizeLHS, sizeLHS);
  if(A.n_rows > 0){ // equality constraints
    LHS.submat(ne, 0, sizeLHS - 1, ne - 1) = A(span::all, span(0, ne - 1));
    LHS.submat(0, ne, ne - 1, sizeLHS - 1) = A(span::all, span(0, ne - 1)).t();
  }
  std::vector<mat> FGP;
  std::vector<std::map<std::string,mat> > WList, WEpi;
  // Setting control parameters
  Rcpp::List params(ctrl.get_params());
  bool trace = Rcpp::as<bool>(params["trace"]);
  int maxiters = Rcpp::as<int>(params["maxiters"]);
  double atol = Rcpp::as<double>(params["abstol"]);
  double ftol = Rcpp::as<double>(params["feastol"]);
  double rtol = Rcpp::as<double>(params["reltol"]);
  double sadj = Rcpp::as<double>(params["stepadj"]);
  double beta = Rcpp::as<double>(params["beta"]);
  //
  // Starting iterations
  //
  for(int i = 0; i < maxiters; i++){
    H.zeros();
    for(int j = 0; j < mnl; j++){
      FGP = fgp(pdv->x(span(0, ne - 1), span::all), FList[j], gList[j]);
      // Setting f to first mnl-rows of h-matrix
      cList.h(j, 0) = FGP[0].at(0, 0);
      // Setting Df to first mnl-rows of G-matrix
      cList.G(j, span(0, ne - 1)) = FGP[1].t();
      // Computing Hessian
      H += pdv->z.at(j, 0) * FGP[2];
    }
    cList.h(0, 0) = cList.h(0, 0) - pdv->x.at(n - 1, 0);
    /*
    Rcpp::Rcout << "Fvals:" << std::endl;
    cList.h.print();
    Rcpp::Rcout << "Gradient:" << std::endl;
    cList.G.print();
    Rcpp::Rcout << "Hessian:" << std::endl;
    H.print();
    */
    // Computing gap
    gap = sum(cList.sdot(pdv->s, pdv->z));
    // Computing residuals
    // Dual Residuals
    rx = cList.G.t() * pdv->z + A.t() * pdv->y;
    rx.at(rx.n_rows - 1, 0) += 1.0;
    resx = norm(rx);
    // Primal Residuals
    ry = b - A * pdv->x;
    resy = norm(ry);
    // Central Residuals 
    rz(span(cList.sidx.at(0, 0), cList.sidx.at(0, 1)), span::all) =
      pdv->s(span(cList.sidx.at(0, 0), cList.sidx.at(0, 1)), span::all) + 
      cList.h(span(cList.sidx.at(0, 0), cList.sidx.at(0, 1)), span::all);
    if(cList.K > 1){
      rz(span(cList.sidx.at(1, 0), cList.sidx.at(1, 1)), span::all) =
	pdv->s(span(cList.sidx.at(1, 0), cList.sidx.at(1, 1)), span::all) + 
	cList.G(span(cList.sidx.at(1, 0), cList.sidx.at(1, 1)), span::all) * pdv->x -
	cList.h(span(cList.sidx.at(1, 0), cList.sidx.at(1, 1)), span::all);
    }
    resz = cList.snrm2(rz);
    // Statistics for stopping criteria
    pcost = pdv->x.at(n - 1, 0);
    dcost = pcost + dot(ry, pdv->y) + sum(cList.sdot(rz, pdv->z)) - gap;
    rgap = NA_REAL;
    if(pcost < 0.0) rgap = gap / (-pcost);
    if(dcost > 0.0) rgap = gap / dcost;
    pres = sqrt(resy * resy + resz * resz);
    dres = resx;
    if(i == 0){
      pres0 = std::max(1.0, pres);
      dres0 = std::max(1.0, dres);
    }
    pres = pres / pres0;
    dres = dres / dres0;
    // Tracing status quo of IPM
    if(trace){
      Rcpp::Rcout << "Iteration: " << i << std::endl;
      Rcpp::Rcout << "pobj: " << pcost << std::endl;
      Rcpp::Rcout << "dobj: " << dcost << std::endl;
      Rcpp::Rcout << "pinf: " << pres << std::endl;
      Rcpp::Rcout << "dinf: " << dres << std::endl;
      Rcpp::Rcout << "dgap: " << gap << std::endl;
      Rcpp::Rcout << std::endl;
    }
    // Checking convergence
    if(!std::isnan(rgap)){
      checkRgap = (rgap <= rtol);
    } else {
      checkRgap = false;
    }
    if((pres <= ftol) && (dres <= ftol) && ((gap <= atol) || checkRgap)){
      ts = cList.smss(pdv->s).max();
      tz = cList.smss(pdv->z).max();
      state["pobj"] = pcost;
      state["dobj"] = dcost;
      state["dgap"] = gap;
      state["certp"] = pres;
      state["certd"] = dres;
      state["pslack"] = -ts;
      state["dslack"] = -tz;
      if(!std::isnan(rgap)){
	state["rgap"] = rgap;
      }
      cps->set_state(state);
      cps->set_status("optimal");
      cps->set_niter(i);
      cps->set_pdv(*pdv);
      cps->pdv.x.reshape(ne, 1); // removing variable 't'
      cps->pdv.x = exp(cps->pdv.x);
      if((mnl > 1) && (cList.K == 1)){ // removing slack variables pertinent to 't'
	cps->pdv.s.set_size(cList.dims[0] - 1, 1);
	cps->pdv.z.set_size(cList.dims[0] - 1, 1);
	cps->pdv.s = pdv->s.submat(1, 0, cList.dims[0] - 1, 0);
	cps->pdv.z = pdv->z.submat(1, 0, cList.dims[0] - 1, 0);
	umat sidxEpi = cList.sidx;
	sidxEpi.at(0, 1) -= 1;
	cps->set_sidx(sidxEpi);
      }
      if((mnl == 1) && (cList.K > 1)){ // removing slack variables pertinent to 't'
	cps->pdv.s.set_size(cList.G.n_rows - 1, 1);
	cps->pdv.z.set_size(cList.G.n_rows - 1, 1);
	cps->pdv.s = pdv->s.submat(1, 0, cList.G.n_rows - 1, 0);
	cps->pdv.z = pdv->z.submat(1, 0, cList.G.n_rows - 1, 0);
	umat sidxEpi = cList.sidx;
	sidxEpi.shed_row(0);
	sidxEpi -= 1;
	sidxEpi.at(0, 0) = 0;
	cps->set_sidx(sidxEpi);
      }
      cps->pdv.s = exp(cps->pdv.s);
      cps->pdv.z = exp(cps->pdv.z);
      if(A.n_rows > 0){
	cps->pdv.y = exp(cps->pdv.y);
      }
      if(trace){
	Rcpp::Rcout << "Optimal solution found." << std::endl;
      }
      return cps;
    }
    // Compute initial scalings
    if(i == 0){
      WList = cList.ntsc(pdv->s, pdv->z);
      Lambda = cList.getLambda(WList);
    }
    LambdaPrd = cList.sprd(Lambda, Lambda);
    LHS.submat(0, 0, ne - 1, ne - 1) = H;
    sigma = 0.0;
    // Finding solution of increments in two-round loop 
    // (same for affine and combined solution)

  } // end i-loop

  // Preparing result for non-convergence in maxiters iterations
  cps->set_pdv(*pdv);
  cps->pdv.x.reshape(ne, 1);    
  cps->pdv.x = exp(cps->pdv.x);
  cps->set_sidx(cList.sidx);
  state["pobj"] = pcost;
  state["dobj"] = dcost;
  state["dgap"] = gap;
  state["certp"] = pres;
  state["certd"] = dres;
  ts = cList.smss(pdv->s).max();
  tz = cList.smss(pdv->z).max();
  state["pslack"] = -ts;
  state["dslack"] = -tz;
  if(!std::isnan(rgap)){
    state["rgap"] = rgap;
  }
  cps->set_state(state);
  cps->set_niter(maxiters);
  cps->set_status("unknown");
  if(trace){
    Rcpp::Rcout << "Optimal solution not determined in " << maxiters << " iteration(s)." << std::endl;
  }
  cps->pdv.s.set_size(cList.G.n_rows - 1, 1);
  cps->pdv.z.set_size(cList.G.n_rows - 1, 1);
  cps->pdv.s = pdv->s.submat(1, 0, cList.G.n_rows - 1, 0);
  cps->pdv.z = pdv->z.submat(1, 0, cList.G.n_rows - 1, 0);
  cps->pdv.s = exp(cps->pdv.s);
  cps->pdv.z = exp(cps->pdv.z);
  if(A.n_rows > 0){
    cps->pdv.y = exp(cps->pdv.y);
  }
  umat sidxEpi = cList.sidx;
  sidxEpi -= 1;
  sidxEpi.at(0, 0) = 0;
  cps->set_sidx(sidxEpi);

  return cps;
}
