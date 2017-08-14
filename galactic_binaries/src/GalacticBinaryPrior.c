//
//  GalacticBinaryPrior.c
//  
//
//  Created by Littenberg, Tyson B. (MSFC-ZP12) on 2/6/17.
//
//
#include <math.h>

#include "LISA.h"
#include "Constants.h"
#include "GalacticBinary.h"
#include "GalacticBinaryIO.h"
#include "GalacticBinaryWaveform.h"
#include "GalacticBinaryPrior.h"


double get_Mc(double f, double dfdt)
{
	return pow( dfdt*pow(f,-11./3.)*(5./96.)*pow(M_PI,-8./3.)  ,  3./5.)/TSUN;
}

double eval_dwd_Mc_comp(double f, double dfdt)
{
		// Refer to Adams, Cornish, Littenberg '12
		double Mc0, a, b, Const, apb, Mc;
		
		// hyper-parameters for prior
		Mc0 = 0.205;
		a 	= 15.;
		b 	= 4.6;
	
		// Normalization constant 
		apb = a + b;
		Const  = Mc0*M_PI;
		Const *= pow(b, (a+1.)/(apb) )*pow(a, -(a+1.)/(apb) );
		Const /= apb*sin(M_PI*(b-1.)/apb);
	
		Mc = get_Mc(f, dfdt);
		
		return 1./Const/(pow(Mc/Mc0,-a) + a*pow(Mc/Mc0,b)/b);
}

double eval_dwd_comp(double f, double dfdt)
{

	double dMc_dfdot;

	if (dfdt < 0.)
	{
		return 0.;
	}
	else 
	{
		// transform to fdot distribution, multiply by dMc_dfdot
		dMc_dfdot = 0.125*pow(3./5.,2./5.)/pow(f,11./5.)/pow(dfdt,2./5.)/pow(M_PI,8./5.)/TSUN;
		
		return dMc_dfdot*eval_dwd_Mc_comp(f, dfdt);
	}
}

double eval_lower_AMCVn_comp(double f, double dfdt)
{
	double model,sigma,a,b,val;
	
	if (dfdt > 0.)
	{
		return 0.;
	}
 	else 
 	{
		a     = 5.2;
		b     = -4.05;
		sigma = 0.2;
		model = a*log10(f) + b;

		val = -0.5*(log10(-dfdt) - model)*(log10(-dfdt) - model);
		val /= sigma*sigma;
		val = exp(val);
		val /= sqrt(PI2)*sigma;

		return val/(-dfdt)/log(10.);
	}
}

double eval_upper_AMCVn_comp(double f, double dfdt)
{
	double model,sigma,a,b,val;
	
	if (dfdt > 0.)
	{
		return 0.;
	}
 	else 
 	{
		a     = 5.8;
		b     = -1.65;
		sigma = 0.13;
		model = a*log10(f) + b;

		val = -0.5*(log10(-dfdt) - model)*(log10(-dfdt) - model);
		val /= sigma*sigma;
		val = exp(val);
		val /= sqrt(PI2)*sigma;

		return val/(-dfdt)/log(10.);
	}
}

double eval_fdot_prior(double *params, double T_obs)
{	
	double N_up_AMCVn, N_low_AMCVn, N_dwd;
	double frac_up, frac_low, frac_dwd;
	double f, dfdt;
	
	f    = params[0]/T_obs;
	dfdt = params[7]/T_obs/T_obs;
	
	// divided by 10 for current AM CVn population considerations
	N_up_AMCVn  = 11197732./10;
	N_low_AMCVn = 23025767./10;
	N_dwd       = 26084422.;
	
	frac_up  = N_up_AMCVn  /(N_up_AMCVn + N_low_AMCVn + N_dwd);
	frac_low = N_low_AMCVn /(N_up_AMCVn + N_low_AMCVn + N_dwd);
	frac_dwd = N_dwd	   /(N_up_AMCVn + N_low_AMCVn + N_dwd);
	
	return frac_up*eval_upper_AMCVn_comp(f, dfdt) + frac_low*eval_lower_AMCVn_comp(f, dfdt) 
									 			 + frac_dwd*eval_dwd_comp(f, dfdt);
}


static double loglike(double *x, int D)
{
  double u, rsq, z, s, ll;
  
  z = x[2];
  rsq = x[0]*x[0]+x[1]*x[1]+x[2]*x[2];
  u = sqrt(x[0]*x[0]+x[1]*x[1]);
  
  s = 1.0/cosh(z/GALAXY_Zd);
  
  // Note that overall rho0 in density is irrelevant since we are working with ratios of likelihoods in the MCMC
  
  ll = log(GALAXY_A*exp(-rsq/(GALAXY_Rb*GALAXY_Rb))+(1.0-GALAXY_A)*exp(-u/GALAXY_Rd)*s*s);
  
  return(ll);
  
}


static void rotate_galtoeclip(double *xg, double *xe)
{
  xe[0] = -0.05487556043*xg[0] + 0.4941094278*xg[1] - 0.8676661492*xg[2];
  
  xe[1] = -0.99382137890*xg[0] - 0.1109907351*xg[1] - 0.00035159077*xg[2];
  
  xe[2] = -0.09647662818*xg[0] + 0.8622858751*xg[1] + 0.4971471918*xg[2];
}

void set_galaxy_prior(struct Flags *flags, struct Prior *prior)
{
  fprintf(stdout,"\n============ Galaxy model sky prior ============\n");
  fprintf(stdout,"Monte carlo over galaxy model\n");
  fprintf(stdout,"   Distance to GC = %g kpc\n",GALAXY_RGC);
  fprintf(stdout,"   Disk Radius    = %g kpc\n",GALAXY_Rd);
  fprintf(stdout,"   Disk Height    = %g kpc\n",GALAXY_Zd);
  fprintf(stdout,"   Bulge Radius   = %g kpc\n",GALAXY_Rb);
  fprintf(stdout,"   Bulge Fraction = %g\n",    GALAXY_A);
  
  double *x, *y;  // current and proposed parameters
  int D = 3;  // number of parameters
  int Nth = 200;  // bins in cos theta
  int Nph = 200;  // bins in phi
  int MCMC=100000000;
  int j;
  int ith, iph, cnt;
  double H, dOmega;
  double logLx, logLy;
  double alpha, beta, xx, yy, zz;
  double *xe, *xg;
  double r_ec, theta, phi;
  int mc;
  //  FILE *chain = NULL;
  
  if(flags->debug)
  {
    Nth /= 10;
    Nph /= 10;
    MCMC/=10;
  }
  
  const gsl_rng_type * T;
  gsl_rng * r;
  gsl_rng_env_setup();
  
  T = gsl_rng_default;
  r = gsl_rng_alloc (T);
  
  x =  (double*)malloc(sizeof(double)* D);
  xe = (double*)malloc(sizeof(double)* D);
  xg = (double*)malloc(sizeof(double)* D);
  y =  (double*)malloc(sizeof(double)* D);
  
  prior->skyhist = (double*)malloc(sizeof(double)* (Nth*Nph));
  
  prior->dcostheta = 2./(double)Nth;
  prior->dphi      = 2.*M_PI/(double)Nph;
  
  prior->ncostheta = Nth;
  prior->nphi      = Nph;
  
  prior->skymaxp = 0.0;
  
  for(ith=0; ith< Nth; ith++)
  {
    for(iph=0; iph< Nph; iph++)
    {
      prior->skyhist[ith*Nph+iph] = 0.0;
    }
  }
  
  // starting values for parameters
  x[0] = 0.5;
  x[1] = 0.4;
  x[2] = 0.1;
  
  logLx = loglike(x, D);
  
  cnt = 0;
  
  //  if(flags->verbose)chain = fopen("chain.dat", "w");
  
  //int MCMC=1000000000;
  for(mc=0; mc<MCMC; mc++)
  {
    if(mc%(MCMC/100)==0)printProgress((double)mc/(double)MCMC);
    
    alpha = gsl_rng_uniform(r);
    
    if(alpha > 0.7)  // uniform draw from a big box
    {
      
      y[0] = 20.0*GALAXY_Rd*(-1.0+2.0*gsl_rng_uniform(r));
      y[1] = 20.0*GALAXY_Rd*(-1.0+2.0*gsl_rng_uniform(r));
      y[2] = 40.0*GALAXY_Zd*(-1.0+2.0*gsl_rng_uniform(r));
      
    }
    else
    {
      
      beta = gsl_rng_uniform(r);
      
      if(beta > 0.8)
      {
        xx = 0.1;
      }
      else if (beta > 0.4)
      {
        xx = 0.01;
      }
      else
      {
        xx = 0.001;
      }
      for(j=0; j< 2; j++) y[j] = x[j] + gsl_ran_gaussian(r,xx);
      y[2] = x[2] + 0.1*gsl_ran_gaussian(r,xx);
      
    }
    
    
    logLy = loglike(y, D);
    
    H = logLy - logLx;
    beta = gsl_rng_uniform(r);
    beta = log(beta);
    
    if(H > beta)
    {
      for(j=0; j< D; j++) x[j] = y[j];
      logLx = logLy;
    }
    
    if(mc%100 == 0)
    {
      
      /* rotate from galactic to ecliptic coordinates */
      
      xg[0] = x[0] - GALAXY_RGC;   // solar barycenter is offset from galactic center along x-axis (by convention)
      xg[1] = x[1];
      xg[2] = x[2];
      
      rotate_galtoeclip(xg, xe);
      
      r_ec = sqrt(xe[0]*xe[0]+xe[1]*xe[1]+xe[2]*xe[2]);
      
      theta = M_PI/2.0-acos(xe[2]/r_ec);
      
      phi = atan2(xe[1],xe[0]);
      
      if(phi<0.0) phi += 2.0*M_PI;
      
      //      if(mc%1000 == 0 && flags->verbose) fprintf(chain,"%d %e %e %e %e %e %e %e\n", mc/1000, logLx, x[0], x[1], x[2], theta, phi, r_ec);
      
      //ith = (int)(0.5*(1.0+sin(theta))*(double)(Nth));
      //iph = (int)(phi/(2.0*M_PI)*(double)(Nph));
      ith = (int)(0.5*(1.0-sin(theta))*(double)(Nth));
      iph = (int)((2*M_PI-phi)/(2.0*M_PI)*(double)(Nph));
      
      //ith = (int)floor(Nth*gsl_rng_uniform(r));
      //iph = (int)floor(Nph*gsl_rng_uniform(r));
      
      cnt++;
      
      if(ith < 0 || ith > Nth -1) printf("%d %d\n", ith, iph);
      if(iph < 0 || iph > Nph -1) printf("%d %d\n", ith, iph);
      
      prior->skyhist[ith*Nph+iph] += 1.0;
      
    }
    
  }
  
  //  if(flags->verbose)fclose(chain);
  
  dOmega = 4.0*M_PI/(double)(Nth*Nph);
  
  double uni = 0.1;
  //fprintf(stderr,"\n   HACK:  setup_galaxy_prior() uni=%g\n",uni);
  yy = (1.0-uni)/(double)(cnt);
  zz = uni/(double)(Nth*Nph);
  
  yy /= dOmega;
  zz /= dOmega;
  
  for(ith=0; ith< Nth; ith++)
  {
    for(iph=0; iph< Nph; iph++)
    {
      xx = yy*prior->skyhist[ith*Nph+iph];
      //if(fabs(xx)  > 0.0) printf("%e %e %e\n", xx, zz, yy);
      prior->skyhist[ith*Nph+iph] = log(xx + zz);
      if(prior->skyhist[ith*Nph+iph]>prior->skymaxp) prior->skymaxp = prior->skyhist[ith*Nph+iph];
    }
  }
  
  if(flags->verbose)
  {
    FILE *fptr = fopen("skyprior.dat", "w");
    for(ith=0; ith< Nth; ith++)
    {
      xx = -1.0+2.0*((double)(ith)+0.5)/(double)(Nth);
      //xx *= -1.0;    // plotting flip?
      for(iph=0; iph< Nph; iph++)
      {
        yy = 2.0*M_PI*((double)(iph)+0.5)/(double)(Nph);
        //yy = 2.0*M_PI - yy;  // plotting flip?
        fprintf(fptr,"%e %e %e\n", yy, xx, prior->skyhist[ith*Nph+iph]);
      }
      fprintf(fptr,"\n");
    }
    fclose(fptr);
  }
  
  free(x);
  free(y);
  free(xe);
  free(xg);
  gsl_rng_free (r);
  fprintf(stdout,"\n================================================\n\n");
  fflush(stdout);
  
}

void set_uniform_prior(struct Flags *flags, struct Model *model, struct Data *data, int verbose)
{
  /*
  params[0] = source->f0*T;
  params[1] = source->costheta;
  params[2] = source->phi;
  params[3] = log(source->amp);
  params[4] = source->cosi;
  params[5] = source->psi;
  params[6] = source->phi0;
  params[7] = source->dfdt*T*T;
  */
  
  
  //TODO:  make t0 a parameter
  for(int i=0; i<model->NT; i++)
  {
    model->t0[i] = data->t0[i];
    model->t0_min[i] = data->t0[i] - 20.0;
    model->t0_max[i] = data->t0[i] + 20.0;
  }
  
  //TODO: assign priors by parameter name, use mapper to get into vector (more robust to changes)
  
  //frequency bin
  //double qpad = 10;
  model->prior[0][0] = data->qmin;//+qpad;
  model->prior[0][1] = data->qmax;//-qpad;
  
  //colatitude
  model->prior[1][0] = -1.0;
  model->prior[1][1] =  1.0;
  
  //longitude
  model->prior[2][0] = 0.0;
  model->prior[2][1] = PI2;
  
  //log amplitude
  model->prior[3][0] = -55.0;//-54
  model->prior[3][1] = -45.0;
  
  //cos inclination
  model->prior[4][0] = -1.0;
  model->prior[4][1] =  1.0;

  //polarization
  model->prior[5][0] = 0.0;
  model->prior[5][1] = PI2;

  //phase
  model->prior[6][0] = 0.0;
  model->prior[6][1] = PI2;
  
  //fdot (bins/Tobs)
  
  /* frequency derivative priors are a little trickier...*/
  double fmin = model->prior[0][0]/data->T;
  double fmax = model->prior[0][1]/data->T;
  
  /* emprical envolope functions from Gijs' MLDC catalog */
  double fdotmin = -0.000005*pow(fmin,(13./3.));
  double fdotmax = 0.0000008*pow(fmax,(11./3.));
  
  /* use prior on chirp mass to convert to priors on frequency evolution */
  if(flags->detached)
  {
    double Mcmin = 0.15;
    double Mcmax = 1.00;
    
    fdotmin = galactic_binary_fdot(Mcmin, fmin, data->T);
    fdotmax = galactic_binary_fdot(Mcmax, fmax, data->T);
  }

  double fddotmin = 11.0/3.0*fdotmin*fdotmin/fmax;
  double fddotmax = 11.0/3.0*fdotmax*fdotmax/fmin;
  
  if(verbose)
  {
    fprintf(stdout,"\n============== PRIORS ==============\n");
    if(flags->detached)fprintf(stdout,"  Assuming detached binary, Mchirp = [0.15,1]\n");
    fprintf(stdout,"  p(fdot)  = U[%g,%g]\n",fdotmin,fdotmax);
    fprintf(stdout,"  p(fddot) = U[%g,%g]\n",fddotmin,fddotmax);
    fprintf(stdout,"====================================\n\n");
  }

  if(data->NP>7)
  {
    model->prior[7][0] = fdotmin*data->T*data->T;
    model->prior[7][1] = fdotmax*data->T*data->T;
  }
  if(data->NP>8)
  {
    model->prior[8][0] = fddotmin*data->T*data->T*data->T;
    model->prior[8][1] = fddotmax*data->T*data->T*data->T;
  }

  //set prior volume
  model->logPriorVolume = 0.0;
  for(int n=0; n<data->NP; n++) model->logPriorVolume -= log(model->prior[n][1]-model->prior[n][0]);
  
}

double evaluate_prior(struct Flags *flags, struct Model *model, struct Prior *prior, double *params, struct Data *data)
{
  double logP=0.0;
  double **uniform_prior = model->prior;
  
  //frequency bin (uniform)
  if(params[0]<uniform_prior[0][0] || params[0]>uniform_prior[0][1]) return -INFINITY;
  else logP -= log(uniform_prior[0][1]-uniform_prior[0][0]);
  
  if(flags->skyPrior)
  {
    if(params[1]<uniform_prior[1][0] || params[1]>uniform_prior[1][1]) return -INFINITY;

    while(params[2] < uniform_prior[2][0]) params[2] += uniform_prior[2][1]-uniform_prior[2][0];
    while(params[2] > uniform_prior[2][1]) params[2] -= uniform_prior[2][1]-uniform_prior[2][0];

    //map costheta and phi to index of skyhist array
    int i = (int)floor((params[1]-uniform_prior[1][0])/prior->dcostheta);
    int j = (int)floor((params[2]-uniform_prior[2][0])/prior->dphi);
    
    int k = i*prior->nphi + j;
    
    logP += prior->skyhist[k];
    
//    FILE *fptr = fopen("prior.dat","a");
//    fprintf(fptr,"%i %i %i %g\n",i,j,k,prior->skyhist[k]);
//    fclose(fptr);
  }
  else
  {
    //colatitude (reflective)
    if(params[1]<uniform_prior[1][0] || params[1]>uniform_prior[1][1]) return -INFINITY;
    else logP -= log(uniform_prior[1][1]-uniform_prior[1][0]);
    
    //longitude (periodic)
    while(params[2] < uniform_prior[2][0]) params[2] += uniform_prior[2][1]-uniform_prior[2][0];
    while(params[2] > uniform_prior[2][1]) params[2] -= uniform_prior[2][1]-uniform_prior[2][0];
    logP -= log(uniform_prior[2][1]-uniform_prior[2][0]);
  }
  
  //log amplitude (step)
  if(params[3]<uniform_prior[3][0] || params[3]>uniform_prior[3][1]) return -INFINITY;
  else logP -= log(uniform_prior[3][1]-uniform_prior[3][0]);
  
  //cosine inclination (reflective)
  if(params[4]<uniform_prior[4][0] || params[4]>uniform_prior[4][1]) return -INFINITY;
  else logP -= log(uniform_prior[4][1]-uniform_prior[4][0]);
  
  //polarization
//  while(params[5] < uniform_prior[5][0]) params[5] += uniform_prior[5][1]-uniform_prior[5][0];
//  while(params[5] > uniform_prior[5][1]) params[5] -= uniform_prior[5][1]-uniform_prior[5][0];
  if(params[5]<uniform_prior[5][0] || params[5]>uniform_prior[5][1]) return -INFINITY;
  else logP -= log(uniform_prior[5][1]-uniform_prior[5][0]);

  //phase
//  while(params[6] < uniform_prior[6][0]) params[6] += uniform_prior[6][1]-uniform_prior[6][0];
//  while(params[6] > uniform_prior[6][1]) params[6] -= uniform_prior[6][1]-uniform_prior[6][0];
  if(params[6]<uniform_prior[6][0] || params[6]>uniform_prior[6][1]) return -INFINITY;
  else logP -= log(uniform_prior[6][1]-uniform_prior[6][0]);

  //fdot (bins/Tobs)
  if(model->NP>7)
  {
  	if(params[7]<uniform_prior[7][0] || params[7]>uniform_prior[7][1]) return -INFINITY;
  	else 
  	{
		if(flags->psynth_fdot_prior == 1)
		{
			logP += log(eval_fdot_prior(params, data->T));
		}
		else
		{
		
			logP -= log(uniform_prior[7][1]-uniform_prior[7][0]);
		}
    }
  }
  
  //fddot
  if(model->NP>8)
  {
    if(params[8]<uniform_prior[8][0] || params[8]>uniform_prior[8][1]) return -INFINITY;
    else logP -= log(uniform_prior[8][1]-uniform_prior[8][0]);
  }
  
  return logP;
}

double evaluate_snr_prior(struct Prior *prior, struct Model *model, double *params, double *Snf, double Tobs, double fstar)
{
  double logP;
  double dfac, dfac5;

  double SNRpeak = 5.0;
  
  double f   = params[0]/Tobs;
  double amp = exp(params[3]);

  // x/a^2 exp(-x/a) prior on SNR. Peaks at x = a. Good choice is a=5
  
  int n = (int)floor(params[0] - model->prior[0][0]);
  double sf = sin(f/fstar); //sin(f/f*)
  double sn = Snf[n];
  double sqT = sqrt(Tobs);
  
  //Sinc spreading
  double SNm  = sn/(4.*sf*sf);   //Michelson noise
  double SNR = amp*sqT/sqrt(SNm); //Michelson SNR (w/ no spread)
  
  dfac = 1.+SNR/(4.*SNRpeak);
  dfac5 = dfac*dfac*dfac*dfac*dfac;
  
  logP = log((3.*SNR)/(4.*SNRpeak*SNRpeak*dfac5)) + log(SNR/params[3]);

  return logP;
}


