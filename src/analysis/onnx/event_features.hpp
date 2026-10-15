#pragma once
#include "dsp.hpp"
namespace ladio::mobile {
// Independently computed Slaney mel features. The supplied models expect
// magnitude (not power), amplitude dB and A weighting. Both bank sizes share
// one FFT. No vendor DSP code, coefficient table or framework is loaded.
class EventFeatures {
    struct Bank {std::vector<std::vector<std::pair<int,double>>> weights;std::vector<double> offsets;};
    Bank activity,structure;std::vector<std::complex<double>> spectrum;
    std::array<double,2048> window{};
    static double mel(double hz){return hz<1000?hz/(200./3):15+std::log(hz/1000)/(std::log(6.4)/27);}
    static double hz(double m){return m<15?m*(200./3):1000*std::exp((m-15)*std::log(6.4)/27);}
    static Bank bank(int count){
        Bank b;b.weights.resize(count);b.offsets.resize(count);std::vector<double> edges(count+2);
        double lo=mel(27.5),hi=mel(12000);
        for(int i=0;i<count+2;i++)edges[i]=hz(lo+(hi-lo)*i/(count+1));
        for(int i=0;i<count;i++){
            for(int k=0;k<=1024;k++){double f=k*24000./2048,w=std::max(0.,std::min((f-edges[i])/(edges[i+1]-edges[i]),(edges[i+2]-f)/(edges[i+2]-edges[i+1])))*2/(edges[i+2]-edges[i]);if(w>0)b.weights[i].push_back({k,w});}
            // Weighting uses a separate inclusive grid, as verified against
            // the local reference frontend. It is not the triangle centre.
            double f=hz(lo+(hi-lo)*i/(count-1)),f2=f*f;
            double r=std::pow(12200.,2)*f2*f2/((f2+20.6*20.6)*std::sqrt((f2+107.7*107.7)*(f2+737.9*737.9))*(f2+12200.*12200.));
            b.offsets[i]=std::max(-80.,20*std::log10(r)+2.);
        }return b;
    }
public:
    EventFeatures():activity(bank(128)),structure(bank(256)),spectrum(2048){
        for(int k=0;k<2048;k++)window[k]=.5-.5*std::cos(2*pi*k/2048);
    }
    void frame(const float*input,size_t count,std::vector<float>&a,std::vector<float>&s){
        for(int k=0;k<2048;k++)spectrum[k]=(size_t(k)<count?input[k]:0)*window[k];
        fft(spectrum);std::array<double,1025> magnitude{};for(int k=0;k<=1024;k++)magnitude[k]=std::abs(spectrum[k]);
        auto append=[&](const Bank&b,std::vector<float>&out){for(size_t i=0;i<b.weights.size();i++){double v=0;for(auto [k,w]:b.weights[i])v+=magnitude[k]*w;out.push_back(float(20*std::log10(std::max(1e-10,v))+b.offsets[i]));}};
        append(activity,a);append(structure,s);
    }
    static constexpr const char* version="magnitude-slaney-A-dB-24k-v1";
};
}
