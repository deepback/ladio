#pragma once
#include "types.hpp"
namespace ladio {
// Independently authored diffused feedback network. Short early reflections
// and a denser late field are separate; decay is specified as RT60 in seconds.
// Buffers are allocated off the render path and remain bounded at every rate.
class DJRoom {
    using Stereo=std::array<float,2>;
    struct Diffuser {
        std::vector<Stereo> data;size_t pos=0;
        Stereo tick(Stereo x)noexcept {Stereo y{};for(int c=0;c<2;c++){y[c]=data[pos][c]-.62f*x[c];data[pos][c]=x[c]+.62f*y[c];}if(++pos==data.size())pos=0;return y;}
        void clear(){std::fill(data.begin(),data.end(),Stereo{});pos=0;}
    };
    int sr_;std::array<std::vector<Stereo>,8> lines_;
    std::array<size_t,8> pos_{},length_{};
    std::array<std::array<double,2>,8> low_{};
    std::array<double,8> lowDecay_{},highDecay_{};
    std::array<Diffuser,3> diffuse_;
    std::vector<Stereo> early_;size_t earlyPos_=0;
    std::array<size_t,6> earlyTaps_{};
    double damp_,lastMin_=-1,lastMax_=-1,lastLow_=-1,lastHigh_=-1;
public:
    explicit DJRoom(int sr):sr_(sr),early_(size_t(sr*.24)+1){
        for(auto&line:lines_)line.resize(size_t(sr*.24)+1);
        const double d[]={.0043,.0071,.0113};for(int i=0;i<3;i++)diffuse_[i].data.resize(size_t(sr*d[i])+1);
        damp_=1-std::exp(-2*pi*3400/sr);
        configure(.022,.093,3,2);
    }
    void configure(double minimum,double maximum,double low,double high)noexcept {
        if(minimum==lastMin_&&maximum==lastMax_&&low==lastLow_&&high==lastHigh_)return;
        // Delay spacing is our own deterministic distribution, not a vendor IR.
        constexpr double spacing[]={.03,.16,.29,.43,.57,.72,.86,.99};
        for(int k=0;k<8;k++){
            length_[k]=std::clamp<size_t>(size_t(sr_*(minimum+(maximum-minimum)*spacing[k])),1,lines_[k].size());
            pos_[k]%=length_[k];double seconds=double(length_[k])/sr_;
            lowDecay_[k]=std::pow(.001,seconds/low);highDecay_[k]=std::pow(.001,seconds/high);
        }
        for(int k=0;k<6;k++)earlyTaps_[k]=std::clamp<size_t>(size_t(sr_*(minimum+(maximum-minimum)*k/6)),1,early_.size()-1);
        lastMin_=minimum;lastMax_=maximum;lastLow_=low;lastHigh_=high;
    }
    void clear(){for(auto&line:lines_)std::fill(line.begin(),line.end(),Stereo{});for(auto&d:diffuse_)d.clear();std::fill(early_.begin(),early_.end(),Stereo{});pos_={};low_={};earlyPos_=0;}
    Stereo process(Stereo input)noexcept {
        Stereo early{};early_[earlyPos_]=input;
        for(int k=0;k<6;k++){auto x=early_[(earlyPos_+early_.size()-earlyTaps_[k])%early_.size()];for(int c=0;c<2;c++)early[c]+=x[(c+k)%2]*(k%2?-.065f:.095f);}
        if(++earlyPos_==early_.size())earlyPos_=0;
        for(auto&d:diffuse_)input=d.tick(input);
        std::array<std::array<double,2>,8> feedback{};Stereo output=early;
        for(int k=0;k<8;k++)for(int c=0;c<2;c++){
            double x=lines_[k][pos_[k]][c];low_[k][c]+=damp_*(x-low_[k][c]);
            feedback[k][c]=lowDecay_[k]*low_[k][c]+highDecay_[k]*(x-low_[k][c]);
            output[c]+=float(x*(k%2?-.21:.21));
        }
        // Orthogonal Householder feedback retains a smooth, bounded decay.
        for(int c=0;c<2;c++){
            double sum=0;for(auto&x:feedback)sum+=x[c];
            for(int k=0;k<8;k++){
                double injection=(input[c]*(k%2?-.31:.31)+input[1-c]*.07);
                lines_[k][pos_[k]][c]=float(std::clamp(injection+feedback[k][c]-.25*sum,-8.,8.));
            }
        }
        for(int k=0;k<8;k++)if(++pos_[k]==length_[k])pos_[k]=0;
        return output;
    }
};
}
