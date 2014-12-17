#include "CPG.h"

/*
 *
 * Methods for Quadratic Programs
 *
*/


/*
Primal objective
*/
double DQP::pobj(PDV& pdv){
  double ans;
  arma::mat term1(1,1);
  term1(0,0) = 0.0;

  term1 = (0.5 * pdv.x.t() * P * pdv.x);
  ans = term1(0,0) + arma::dot(pdv.x, q);

  return ans;
}
/*
Dual objective
*/
double DQP::dobj(PDV& pdv){
  double ans;
  arma::mat term1(1,1), term2(1,1);
  term1(0,0) = 0.0;
  term2(0,0) = 0.0;

  // dobj term for equality constraints
  term1 = pdv.y.t() * (A * pdv.x - b);
  // dobj term for inequality constraints
  if(cList.K > 0){
    for(int i = 0; i < cList.K; i++){
      term2 = term2 + pdv.z[i].t() *			\
	(cList.Gmats[i] * pdv.x - cList.hvecs[i]);
    } 
  }
  ans = pobj(pdv) + term1(0,0) + term2(0,0);

  return ans;
}
/*
Primal Residuals
*/
arma::mat DQP::rprim(PDV& pdv){
  int p = A.n_rows;
  arma::mat ans(p,1);
  ans.zeros();

  ans = b - A * pdv.x;

  return ans;
}
/*
Centrality Resdiuals
*/
std::vector<arma::mat> DQP::rcent(PDV& pdv){
  std::vector<arma::mat> ans;

  for(int i = 0; i < cList.K; i++){
    ans[i] = pdv.s[i] + cList.Gmats[i] * pdv.x - cList.hvecs[i];
  }

  return ans;
}
/*
Dual Residuals
*/
arma::mat DQP::rdual(PDV& pdv){
  int n = P.n_rows;
  arma::mat Gz(n,1);
  arma::mat Ay(n,1);
  arma::mat ans(n,1);
  Gz.zeros();
  Ay.zeros();
  ans.zeros();

  if(cList.K > 0){
    for(int i = 0; i < cList.K; i++){
      Gz = Gz + cList.Gmats[i].t() * pdv.z[i];
    } 
  }
  if(A.n_rows > 0){
    Ay = A.t() * pdv.y;
  }
  ans = P * pdv.x + q + Gz + Ay;

  return ans;
}
/*
Certificate of primal infeasibilty
*/
double DQP::certp(PDV& pdv){
  double nomin, denom, ans1 = 0.0, ans2 = 0.0, ans = 0.0;

  nomin = arma::norm(rprim(pdv));
  denom = std::max(1.0, arma::norm(b));
  ans1 = nomin / denom;

  if(cList.K > 0){
    std::vector<arma::mat> rz;
    rz = rcent(pdv);
    nomin = 0.0;
    denom = std::max(1.0, arma::norm(q));
    for(int i = 0; i < cList.K; i++){
      nomin += arma::norm(rz[i]);
    } 
    ans2 = nomin / denom;
  }
  ans = std::max(ans1, ans2);

  return ans;
}
/*
Certificate of dual infeasibilty
*/
double DQP::certd(PDV& pdv){
  double nomin, denom, ans;

  int n = P.n_rows;
  arma::mat Gz(n,1);
  Gz.zeros();

  if(cList.K > 0){
    for(int i = 0; i < cList.K; i++){
      Gz = Gz + cList.Gmats[i].t() * pdv.z[i];
    } 
  }

  nomin = arma::norm(P * pdv.x + Gz + A.t() * pdv.y + q);
  denom = std::max(1.0, arma::norm(q));
  ans = nomin / denom;

  return ans;
}
/*
Initializing PDV
*/
PDV* DQP::initpdv(){
  PDV* pdv = new PDV();
  std::vector<arma::mat> s, z;
  arma::mat ans;
  int n = P.n_cols;

  pdv->x = arma::zeros(n,1);
  pdv->y = arma::zeros(A.n_rows,1);
  for(int i = 0; i < cList.K; i++){
    if((cList.conTypes[i] == "NLFC") || (cList.conTypes[i] == "NNOC")){
      ans = arma::ones(n, 1);
    } else if(cList.conTypes[i] == "SOCC"){
      ans = arma::zeros(n, 1);
      ans.at(0,0) = 1.0;
    } else if(cList.conTypes[i] == "PSDC") {
      ans = arma::eye(n,n);
      ans.reshape(n * n, 1);
    } else {
      ans = arma::zeros(cList.dims[i], 1);
    }
    s[i] = ans;
    z[i] = ans;
  }
  pdv->s = s;
  pdv->z = s;
  pdv->tau = 1.0;
  pdv->kappa = 1.0;

  return pdv;
}
/*
  Main routine for solving a Quadratic Program
*/
CPS* DQP::cps(CTRL& ctrl){
  // Initialising object
  PDV* pdv = initpdv();
  CPS* cps = new CPS();
  Rcpp::NumericVector state = cps->get_state();
  // Case 1: Unconstrained QP
  if((cList.K == 0) && (A.n_rows == 0)){
    pdv->x = solve(P, -q);
    cps->set_pdv(*pdv);
    state["pobj"] = pobj(*pdv);
    cps->set_state(state);
    cps->set_status("optimal");
  }
  // Case 2: Equality constrained QP
  if((cList.K == 0) && (A.n_rows > 0)){
    arma::mat Pi, PiA, Piq, S, Si;
    double ftol = ctrl.get_feastol();
    try{
      Pi = inv(P);
    } catch(std::runtime_error &ex){
      forward_exception_to_r(ex);
    } catch(...){
      ::Rf_error("C++ exception (unknown reason)");
    }
    Piq = Pi * q;
    PiA = Pi * A.t();
    S = -A * PiA;
    try{
      Si = inv(S);
    } catch(std::runtime_error &ex){
      throw std::range_error("Inversion of Schur complement failed.");
      forward_exception_to_r(ex);
    } catch(...) {
      ::Rf_error("C++ exception (unknown reason)");
    }

    pdv->y = Si * (A * Piq + b);
    pdv->x = Pi * (-(A.t() * pdv->y) - q);
    cps->set_pdv(*pdv);
    state["pobj"] = pobj(*pdv);
    state["dobj"] = dobj(*pdv);
    state["certp"] = certp(*pdv);
    state["certd"] = certd(*pdv);
    cps->set_state(state);
    if((state["certp"] <= ftol) && (state["certd"] <= ftol)){
      cps->set_status("optimal");
    } else {
      cps->set_status("unknown");
    }
  }
  // Case 3: At least inequality constrained QP
  // Initialising variables
  int m = sum(cList.dims);
  std::map<std::string,double> cvgdvals;
  cvgdvals["pobj"] = NA_REAL;
  cvgdvals["dobj"] = NA_REAL;
  cvgdvals["pinf"] = NA_REAL;
  cvgdvals["dinf"] = NA_REAL;
  cvgdvals["dgap"] = NA_REAL;
  std::vector<std::map<std::string,arma::mat> > WList;

 
  return cps;
}
