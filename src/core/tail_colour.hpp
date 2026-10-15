#pragma once
#include "scene.hpp"
namespace ladio {
// Independent, bounded tail processors. Coefficients are compiled with the
// scene. Processing allocates nothing and never modifies a deck's playhead.
class TailColour {
    std::vector<std::array<float,2>> delay_;
    std::array<double,2> z1_{},z2_{},low_{},high_{};
    size_t position_=0;
    int sr_;
public:
    explicit TailColour(int sr):delay_(size_t(sr*.025)+2),sr_(sr){}
    void clear()noexcept{std::fill(delay_.begin(),delay_.end(),std::array<float,2>{});z1_={};z2_={};low_={};high_={};position_=0;}
    std::array<float,2> process(std::array<float,2> in,const Scene::EffectChain&fx,double quarter)noexcept{
        std::array<float,2> out{};
        double length=fx.flangerWet>0?sr_*(.003+.0025*std::sin(2*pi*quarter/fx.flangerQuarters)):1.;
        size_t whole=size_t(length),index=(position_+delay_.size()-whole)%delay_.size(),previous=(index+delay_.size()-1)%delay_.size();
        double fraction=length-whole,phase=std::fmod(std::max(0.,quarter)/fx.gateQuarters,1.);
        // Raised edges avoid clicks; a bounded depth leaves the pulse intact.
        double gate=1-fx.gateDepth*(1-smooth(phase/.08)*(1-smooth((phase-fx.gateDuty)/.08)));
        for(int c=0;c<2;c++){
            double y=fx.eq[0]*in[c]+z1_[c];z1_[c]=fx.eq[1]*in[c]-fx.eq[3]*y+z2_[c];z2_[c]=fx.eq[2]*in[c]-fx.eq[4]*y;
            double tapped=delay_[index][c]*(1-fraction)+delay_[previous][c]*fraction;
            delay_[position_][c]=float(std::clamp(y+tapped*fx.flangerFeedback,-1.5,1.5));
            y=(y+fx.flangerWet*tapped)/(1+fx.flangerWet);
            low_[c]+=fx.tailHP*(y-low_[c]);y-=low_[c];
            high_[c]+=fx.tailLP*(y-high_[c]);out[c]=float(high_[c]*gate);
        }
        position_=(position_+1)%delay_.size();return out;
    }
};
}
