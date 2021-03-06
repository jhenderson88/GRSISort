#include "TZeroDegreeHit.h"

#include <iostream>
#include <algorithm>
#include <climits>

#include "Globals.h"
#include "TZeroDegree.h"
#include "TGRSIOptions.h"
#include "TChannel.h"

/// \cond CLASSIMP
ClassImp(TZeroDegreeHit)
/// \endcond

TZeroDegreeHit::TZeroDegreeHit()	{
   //Default Constructor
#if MAJOR_ROOT_VERSION < 6
   Class()->IgnoreTObjectStreamer(kTRUE);
#endif
   Clear();
}

TZeroDegreeHit::~TZeroDegreeHit()	{	}

TZeroDegreeHit::TZeroDegreeHit(TFragment &frag) : TGRSIDetectorHit(frag) {
	if(TGRSIOptions::Get()->ExtractWaves()) {
		if(frag.GetWaveform()->size() == 0) {
			printf("Warning, TZeroDegree::SetWave() set, but data waveform size is zero!\n");
		} else {
			frag.CopyWave(*this);
		}
		if(GetWaveform()->size() > 0) {
			AnalyzeWaveform();
		}
	}
}

TZeroDegreeHit::TZeroDegreeHit(const TZeroDegreeHit &rhs) : TGRSIDetectorHit() {
   //Copy Constructor
#if MAJOR_ROOT_VERSION < 6
   Class()->IgnoreTObjectStreamer(kTRUE);
#endif
   Clear();
   rhs.Copy(*this);
}

void TZeroDegreeHit::Copy(TObject &rhs) const {
  ///Copies a TZeroDegreeHit
   TGRSIDetectorHit::Copy(rhs);
	if(TGRSIOptions::Get()->ExtractWaves()) {
	  TGRSIDetectorHit::CopyWave(rhs);
	}
   static_cast<TZeroDegreeHit&>(rhs).fFilter = fFilter;
   static_cast<TZeroDegreeHit&>(rhs).fCfdMonitor = fCfdMonitor;
   static_cast<TZeroDegreeHit&>(rhs).fPartialSum = fPartialSum;
}

bool TZeroDegreeHit::InFilter(Int_t wantedfilter) {
	/// check if the desired filter is in wanted filter;
   /// return the answer;
   return true;
}

Int_t TZeroDegreeHit::GetCfd() const {
	/// special function for TZeroDegreeHit to return CFD after mapping out the high bits
	/// which are the remainder between the 125 MHz data and the 100 MHz timestamp clock
	return fCfd & 0x3fffff;
}

Int_t TZeroDegreeHit::GetRemainder() const {
	/// returns the remainder between 100 MHz/10ns timestamp and 125 MHz/8 ns data in ns
	return fCfd>>22;
}

Double_t TZeroDegreeHit::GetTime(const UInt_t& correction_flag, Option_t* opt) const {
  Double_t dTime = GetTimeStamp()*10.+GetRemainder()+(GetCfd() + gRandom->Uniform())/256.;
  TChannel* chan = GetChannel();
  if(!chan) {
    Error("GetTime","No TChannel exists for address 0x%08x",GetAddress());
    return dTime;
  }

  return dTime - 10.*(chan->GetTZero(GetEnergy()));
}

void TZeroDegreeHit::Clear(Option_t *opt)	{
   ///Clears the ZeroDegreeHit
   fFilter = 0;
   TGRSIDetectorHit::Clear();
	fCfdMonitor.clear();
	fPartialSum.clear();
}

void TZeroDegreeHit::Print(Option_t *opt) const	{
   ////Prints the ZeroDegreeHit. Returns:
   ////Detector
   ////Energy
   ////Time
   printf("ZeroDegree Detector: %i\n",GetDetector());
   printf("ZeroDegree hit energy: %.2f\n",GetEnergy());
   printf("ZeroDegree hit time:   %.lf\n",GetTime());
}

bool TZeroDegreeHit::AnalyzeWaveform() {
   ///Calculates the cfd time from the waveform
   bool error = false;
   std::vector<Short_t> *waveform = GetWaveform();
   if(waveform->empty()) {
      return false; //Error!
   }
   
   std::vector<Int_t> baselineCorrections (8, 0);
   std::vector<Short_t> smoothedWaveform;
   
   // all timing algorithms use interpolation with this many steps between two samples (all times are stored as integers)
   unsigned int interpolationSteps = 256;
   int delay = 2;
   double attenuation = 24./64.;
   int halfsmoothingwindow = 0; //2*halfsmoothingwindow + 1 = number of samples in moving window.
   
   // baseline algorithm: correct each adc with average of first two samples in that adc
   for(size_t i = 0; i < 8 && i < waveform->size(); ++i) {
      baselineCorrections[i] = (*waveform)[i];
   }
   for(size_t i = 8; i < 16 && i < waveform->size(); ++i) {
      baselineCorrections[i-8] = ((baselineCorrections[i-8] + (*waveform)[i]) + ((baselineCorrections[i-8] + (*waveform)[i]) > 0 ? 1 : -1)) >> 1;
   }
   for(size_t i = 0; i < waveform->size(); ++i) {
      (*waveform)[i] -= baselineCorrections[i%8];
   }
   
   SetCfd(CalculateCfd(attenuation, delay, halfsmoothingwindow, interpolationSteps));
   
	SetCharge(CalculatePartialSum().back());

   return !error;
}

Int_t TZeroDegreeHit::CalculateCfd(double attenuation, unsigned int delay, int halfsmoothingwindow, unsigned int interpolationSteps) {
   ///Used when calculating the CFD from the waveform
   std::vector<Short_t> monitor;
   
   return CalculateCfdAndMonitor(attenuation, delay, halfsmoothingwindow, interpolationSteps, monitor);
}

Int_t TZeroDegreeHit::CalculateCfdAndMonitor(double attenuation, unsigned int delay, int halfsmoothingwindow, unsigned int interpolationSteps, std::vector<Short_t> &monitor) {
   ///Used when calculating the CFD from the waveform
   
   Short_t monitormax = 0;
   std::vector<Short_t> *waveform = GetWaveform();
   
   bool armed = false;
   
   Int_t cfd = 0;
   
   std::vector<Short_t> smoothedWaveform;
   
   if(waveform->empty()) {
      return INT_MAX; //Error!
   }
   
   if((unsigned int)waveform->size() > delay+1) {
      
      if(halfsmoothingwindow > 0) {
         smoothedWaveform = TZeroDegreeHit::CalculateSmoothedWaveform(halfsmoothingwindow);
      } else {
         smoothedWaveform = *waveform;
      }
      
      monitor.resize(smoothedWaveform.size()-delay);
      monitor[0] = attenuation*smoothedWaveform[delay]-smoothedWaveform[0];
      if(monitor[0] > monitormax) {
         armed = true;
         monitormax = monitor[0];
      }
      
      for(unsigned int i = delay + 1; i < smoothedWaveform.size(); ++i) {
         monitor[i-delay] = attenuation*smoothedWaveform[i]-smoothedWaveform[i-delay];
         if(monitor[i-delay] > monitormax) {
            armed=true;
            monitormax = monitor[i-delay];
         } else {
            if(armed == true && monitor[i-delay] < 0) {
               armed = false;
               if(monitor[i-delay-1]-monitor[i-delay] != 0) {
                  //Linear interpolation.
                  cfd = (i-delay-1)*interpolationSteps + (monitor[i-delay-1]*interpolationSteps)/(monitor[i-delay-1]-monitor[i-delay]);
               }
               else {
                  //Should be impossible, since monitor[i-delay-1] => 0 and monitor[i-delay] > 0
                  cfd = 0;
               }
            }
         }
      }
   } else {
      monitor.resize(0);
   }
   
	if(TGRSIOptions::Get()->Debug()) {
		fCfdMonitor = monitor;
	}
   
	// correct for remainder between the 100MHz timestamp and the 125MHz start of the waveform
	// we save this in the upper bits, otherwise we can't correct the waveform themselves
	cfd = (cfd & 0x3fffff) | (fCfd & 0x7c00000);
   
   return cfd;
   
}

std::vector<Short_t> TZeroDegreeHit::CalculateSmoothedWaveform(unsigned int halfsmoothingwindow) {
   ///Used when calculating the CFD from the waveform
   
   std::vector<Short_t> *waveform = GetWaveform();
   if(waveform->empty()) {
      return std::vector<Short_t>(); //Error!
   }
   
   std::vector<Short_t> smoothedWaveform(std::max((size_t)0, waveform->size()-2*halfsmoothingwindow), 0);
   
   for(size_t i = halfsmoothingwindow; i < waveform->size()-halfsmoothingwindow; ++i) {
      for(int j = -(int)halfsmoothingwindow; j <= (int)halfsmoothingwindow; ++j) {
         smoothedWaveform[i-halfsmoothingwindow] += (*waveform)[i+j];
      }
   }
   
   return smoothedWaveform;
}

std::vector<Short_t> TZeroDegreeHit::CalculateCfdMonitor(double attenuation, int delay, int halfsmoothingwindow) {
   ///Used when calculating the CFD from the waveform
   
   std::vector<Short_t> *waveform = GetWaveform();
   
   if(waveform->empty()) {
      return std::vector<Short_t>(); //Error!
   }
   
   std::vector<Short_t> smoothedWaveform;
   
   if(halfsmoothingwindow > 0) {
      smoothedWaveform = TZeroDegreeHit::CalculateSmoothedWaveform(halfsmoothingwindow);
   }
   else{
      smoothedWaveform = *waveform;
   }
   
   std::vector<Short_t> monitor(std::max((size_t)0, smoothedWaveform.size()-delay), 0);
   
   for(size_t i = delay; i < smoothedWaveform.size(); ++i) {
      monitor[i-delay] = attenuation*smoothedWaveform[i]-smoothedWaveform[i-delay];
   }
   
   return monitor;
}

std::vector<Int_t> TZeroDegreeHit::CalculatePartialSum() {

	std::vector<Short_t> *waveform = GetWaveform();
	if(waveform->empty()) {
		return std::vector<Int_t>(); //Error!
	}

	std::vector<Int_t> partialSums(waveform->size(), 0);

	if(waveform->size() > 0) {
		partialSums[0] = waveform->at(0);
		for(size_t i = 1; i < waveform->size(); ++i) {
			partialSums[i] = partialSums[i-1] + (*waveform)[i];
		}
	}

	if(TGRSIOptions::Get()->Debug()) {
		fPartialSum = partialSums;
	}

	return partialSums;
}

