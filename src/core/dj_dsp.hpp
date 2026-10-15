#pragma once
#include "dj_program.hpp"
#include "dj_room.hpp"
namespace ladio {
class DJDeckDSP {
    struct SVF {
        double i1=0,i2=0;
        std::array<double,2> process(double x,double g,double q)noexcept {
            double k=1/q,a=1/(1+g*(g+k)),v1=a*(i1+g*(x-i2)),v2=i2+g*v1;
            i1=2*v1-i1;i2=2*v2-i2;
            return {v2,x-k*v1-v2};
        }
    };
    int sr_;size_t delayPos_=0,flangePos_=0,prePos_=0;
    std::vector<std::array<float,2>> delay_,flange_,predelay_;
    std::vector<std::array<float,2>> echo_;
    size_t echoPos_=0;
    DJRoom auxRoom_,performanceRoom_;
    std::array<std::vector<std::array<float,2>>,4> room_;
    std::array<size_t,4> roomPos_{};
    std::array<std::array<double,2>,4> roomLow_{};
    std::array<SVF,2> high_,low_;
    std::array<double,2> eq1_{},eq2_{},tailLow_{},tailHigh_{},delayLow_{},delayHigh_{};
    std::array<double,4> lowDecay_{},highDecay_{};
    double configuredLow_=-1,configuredHigh_=-1,roomDamp_,delayHP_,delayLP_;
    static std::array<float,2> tap(const std::vector<std::array<float,2>>&ring,size_t pos,double length)noexcept{
        length=std::clamp(length,1.,double(ring.size()-2));size_t whole=size_t(length);
        size_t i=(pos+ring.size()-whole)%ring.size(),j=(i+ring.size()-1)%ring.size();float u=float(length-whole);
        return {ring[i][0]+u*(ring[j][0]-ring[i][0]),ring[i][1]+u*(ring[j][1]-ring[i][1])};
    }
public:
    struct Result {std::array<float,2> dry{},wet{};};
    explicit DJDeckDSP(int sr):sr_(sr),delay_(size_t(sr)*8+2),flange_(size_t(sr*.025)+2),predelay_(size_t(sr*.20)+2),echo_(size_t(sr)*8+2),auxRoom_(sr),performanceRoom_(sr){
        constexpr double lengths[]={.0317,.0439,.0593,.0719};
        for(int k=0;k<4;k++)room_[k].resize(size_t(lengths[k]*sr)+1);
        roomDamp_=1-std::exp(-2*pi*3100/sr);delayHP_=1-std::exp(-2*pi*170/sr);delayLP_=1-std::exp(-2*pi*6500/sr);
    }
    void clear(){
        for(auto*ring:{&delay_,&flange_,&predelay_,&echo_})std::fill(ring->begin(),ring->end(),std::array<float,2>{});
        for(auto&ring:room_)std::fill(ring.begin(),ring.end(),std::array<float,2>{});
        delayPos_=flangePos_=prePos_=echoPos_=0;roomPos_={};roomLow_={};high_={};low_={};eq1_={};eq2_={};tailLow_={};tailHigh_={};delayLow_={};delayHigh_={};auxRoom_.clear();performanceRoom_.clear();
    }
    void configure(const DJDeckProgram&p){
        if(p.serial){
            if(p.auxReverbActive)auxRoom_.configure(p.reverbMinSeconds,p.reverbMaxSeconds,p.reverbLowSeconds,p.reverbHighSeconds);
            if(p.roomActive)performanceRoom_.configure(.018,.118,p.roomLowSeconds,p.roomHighSeconds);
            return;
        }
        if(configuredLow_==p.reverbLowSeconds&&configuredHigh_==p.reverbHighSeconds)return;
        for(int k=0;k<4;k++){double seconds=double(room_[k].size())/sr_;lowDecay_[k]=std::pow(.001,seconds/p.reverbLowSeconds);highDecay_[k]=std::pow(.001,seconds/p.reverbHighSeconds);}
        configuredLow_=p.reverbLowSeconds;configuredHigh_=p.reverbHighSeconds;
    }
    Result process(std::array<float,2> input,const DJDeckProgram&p,Frame frame,double quarter)noexcept{
        if(p.serial)return processSerial(input,p,frame,quarter);
        Result result;std::array<float,2> send{},roomInput{};
        const double filterWet=p.filterWet.at(frame),flanger=p.flangerMix.at(frame),depth=p.gateDepth.at(frame);
        const double hp=p.highpass.at(frame),lp=p.lowpass.at(frame);
        const auto flange=tap(flange_,flangePos_,sr_*(.0035+.0028*std::sin(2*pi*quarter/p.flangerQuarters)));
        const double phase=quarter/p.gateQuarters-std::floor(quarter/p.gateQuarters);
        const double gate=1-depth*(1-smooth(phase/.06)*(1-smooth((phase-p.gateDuty)/.06)));
        for(int c=0;c<2;c++){
            double eq=p.eq[0]*input[c]+eq1_[c];eq1_[c]=p.eq[1]*input[c]-p.eq[3]*eq+eq2_[c];eq2_[c]=p.eq[2]*input[c]-p.eq[4]*eq;
            eq=input[c]+p.eqWet.at(frame)*(eq-input[c]);
            flange_[flangePos_][c]=float(std::clamp(eq+flange[c]*p.flangerFeedback,-2.,2.));
            double x=(eq+flanger*flange[c])/(1+flanger);
            x*=gate;
            double filtered=low_[c].process(high_[c].process(x,hp,p.resonance)[1],lp,p.resonance)[0];
            result.dry[c]=float(x+filterWet*(filtered-x));send[c]=float(p.sendPreFilter?x:result.dry[c]);
        }
        flangePos_=(flangePos_+1)%flange_.size();
        auto delayed=tap(delay_,delayPos_,p.delayFrames.at(frame));
        for(int c=0;c<2;c++){
            delayLow_[c]+=delayHP_*(delayed[c]-delayLow_[c]);
            delayHigh_[c]+=delayLP_*(delayed[c]-delayLow_[c]-delayHigh_[c]);
        }
        for(int c=0;c<2;c++){
            delay_[delayPos_][c]=float(std::clamp(send[c]*p.delaySend.at(frame)+p.delayFeedback*(.82*delayHigh_[c]+.18*delayHigh_[1-c]),-2.,2.));
            predelay_[prePos_][c]=float((send[c]+p.delayToReverb*delayHigh_[c])*p.reverbSend.at(frame));
        }
        delayPos_=(delayPos_+1)%delay_.size();roomInput=tap(predelay_,prePos_,std::max(1.,p.predelaySeconds*sr_));prePos_=(prePos_+1)%predelay_.size();
        std::array<std::array<double,2>,4> read{},feedback{};std::array<double,2> roomOutput{};
        for(int k=0;k<4;k++)for(int c=0;c<2;c++){
            read[k][c]=room_[k][roomPos_[k]][c];roomLow_[k][c]+=roomDamp_*(read[k][c]-roomLow_[k][c]);
            feedback[k][c]=lowDecay_[k]*roomLow_[k][c]+highDecay_[k]*(read[k][c]-roomLow_[k][c]);
            roomOutput[c]+=read[k][c]*.25;
        }
        constexpr int signs[4][4]={{1,1,1,1},{1,-1,1,-1},{1,1,-1,-1},{1,-1,-1,1}};
        for(int k=0;k<4;k++){
            for(int c=0;c<2;c++){double mixed=0;for(int j=0;j<4;j++)mixed+=signs[k][j]*feedback[j][c]*.5;
                room_[k][roomPos_[k]][c]=float(std::clamp(roomInput[c]*.46+roomInput[1-c]*.08+mixed,-2.,2.));}
            roomPos_[k]=(roomPos_[k]+1)%room_[k].size();
        }
        for(int c=0;c<2;c++){
            double x=delayHigh_[c]*p.delayReturn.at(frame)+roomOutput[c]*p.reverbReturn.at(frame);
            tailLow_[c]+=p.tailHP*(x-tailLow_[c]);tailHigh_[c]+=p.tailLP*(x-tailLow_[c]-tailHigh_[c]);result.wet[c]=(p.delayReturn.at(frame)==0&&p.reverbReturn.at(frame)==0)?0:float(tailHigh_[c]);
        }
        return result;
    }
private:
    Result processSerial(std::array<float,2> input,const DJDeckProgram&p,Frame frame,double quarter)noexcept{
        Result result;std::array<float,2> x{},send{},aux{},roomInput{};
        double mix=p.flangerMix.at(frame),depth=p.gateDepth.at(frame),filter=p.filterWet.at(frame);
        std::array<float,2> flange{};
        if(mix>0)flange=tap(flange_,flangePos_,sr_*(p.flangerCenterSeconds+p.flangerDepthSeconds*std::sin(2*pi*quarter/p.flangerQuarters)));
        double phase=quarter/p.gateQuarters-std::floor(quarter/p.gateQuarters);
        double gate=1-depth*(1-smooth(phase/.035)*(1-smooth((phase-p.gateDuty)/.035)));
        for(int c=0;c<2;c++){
            double value=input[c]*p.inputGain.at(frame);
            flange_[flangePos_][c]=float(std::clamp(value+flange[c]*p.flangerFeedback,-8.,8.));
            value=((1-mix)*value+mix*flange[c])*gate;
            send[c]=float(value);
            double filtered=low_[c].process(high_[c].process(value,p.highpass.at(frame),p.resonance)[1],p.lowpass.at(frame),p.resonance)[0];
            value+=filter*(filtered-value);
            double eq=p.eq[0]*value+eq1_[c];eq1_[c]=p.eq[1]*value-p.eq[3]*eq+eq2_[c];eq2_[c]=p.eq[2]*value-p.eq[4]*eq;
            x[c]=float(value+p.eqWet.at(frame)*(eq-value));
            if(!p.sendPreFilter)send[c]=x[c];
            roomInput[c]=send[c]*p.roomSend.at(frame);
        }
        if(++flangePos_==flange_.size())flangePos_=0;
        std::array<float,2> room{},echo{};
        if(p.roomActive)room=performanceRoom_.process(roomInput);
        if(p.echoActive){
            echo=tap(echo_,echoPos_,p.delayFrames.at(frame));
            for(int c=0;c<2;c++){
                delayLow_[c]+=p.delayLowpassAlpha*(echo[c]-delayLow_[c]);echo[c]=float(delayLow_[c]);
                echo_[echoPos_][c]=float(std::clamp(send[c]*p.echoSend.at(frame)+p.echoFeedback*echo[c],-8.,8.));
            }
            if(++echoPos_==echo_.size())echoPos_=0;
        }
        for(int c=0;c<2;c++){
            result.dry[c]=x[c]*p.dryGain.at(frame);
            // DJRoom already owns its injection and output normalization. A
            // second fixed pad here buried an otherwise populated room by
            // 8.4 dB, particularly when the new track's full mix arrived.
            result.wet[c]=room[c]*p.roomReturn.at(frame)+echo[c]*p.echoReturn.at(frame);
            aux[c]=(send[c]+result.wet[c])*p.auxSend.at(frame);
        }
        if(p.auxActive){
            auto delayed=tap(delay_,delayPos_,p.delayFrames.at(frame));
            for(int c=0;c<2;c++){
                delayHigh_[c]+=p.delayLowpassAlpha*(delayed[c]-delayHigh_[c]);
                delay_[delayPos_][c]=float(std::clamp(aux[c]+p.delayFeedback*delayHigh_[c],-8.,8.));
                aux[c]=float(aux[c]*(1-p.delayMix.at(frame))+delayHigh_[c]*p.delayMix.at(frame));
            }
            if(++delayPos_==delay_.size())delayPos_=0;
            std::array<float,2> reverbed{};
            // Wet automation controls the return, not the processor clock.
            // A muted room must still receive capture and advance its decay.
            if(p.auxReverbActive)reverbed=auxRoom_.process(aux);
            for(int c=0;c<2;c++){
                double value=aux[c]*(1-p.reverbMix.at(frame))+reverbed[c]*p.reverbMix.at(frame)*p.reverbGain;
                result.wet[c]+=float(value*p.auxReturn.at(frame));
            }
        }
        // One return EQ protects the incoming foundation from every wet path,
        // including performance-room and repeat echoes. The capture tap can
        // precede the dry sweep without bringing A's bass back through its tail.
        const bool returned=p.auxReturn.at(frame)>0||p.roomReturn.at(frame)>0||p.echoReturn.at(frame)>0;
        for(int c=0;c<2;c++){
            tailLow_[c]+=p.tailHP*(result.wet[c]-tailLow_[c]);
            tailHigh_[c]+=p.tailLP*(result.wet[c]-tailLow_[c]-tailHigh_[c]);
            result.wet[c]=returned?float(tailHigh_[c]):0;
        }
        return result;
    }
};
}
