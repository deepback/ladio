#pragma once
#include "types.hpp"
namespace ladio {
struct DJStyle {const char*id;const char*title;int bars;const char*shape;bool repeat=false,delay=false,room=false,autoDJ=true;};
// Musical specifications, independently authored. The reference's resource
// identifiers, plugin state and automation data are not runtime inputs.
inline constexpr std::array<DJStyle,25> djStyles={{
 {"dj_seamless","Непрерывный стык",2,"plain",false,false,false,false},
 {"dj_equal_power","Равномерное наложение",4,"plain",false,false,false,false},
 {"dj_late_entry","Поздний короткий вход",4,"late",false,false,false,false},
 {"dj_splice","Монтаж на границе фразы",2,"splice",false,false,false,false},
 {"dj_overlap","Естественное перекрытие",4,"plain",false,false,false,false},
 {"dj_room_overlap","Перекрытие с пространством",4,"plain",false,false,true,false},
 {"dj_ring_out","Сохранить естественный хвост",4,"late",false,false,false,false},
 {"dj_room_ring","Пространственный хвост",4,"late",false,false,true,false},
 {"dj_short_sweep","Короткая передача спектра",2,"sweep"},
 {"dj_phrase_exchange","Сведение музыкальных фраз",8,"phrase"},
 {"dj_elastic_entry","Подведение к сильному входу",8,"late",false,true},
 {"dj_spectral_sweep","Уход под воду и раскрытие",8,"sweep"},
 {"dj_band_expansion","Расширение полосы B",8,"expand"},
 {"dj_echo_filter","Фильтр и развивающееся эхо",8,"sweep",false,true},
 {"dj_room_filter","Фильтр и пространственный мост",8,"expand",false,false,true},
 {"dj_long_sweep","Длинная передача спектра",16,"sweep"},
 {"dj_soft_room","Мягкое раскрытие через пространство",10,"soft",false,false,true},
 {"dj_voice_clearance","Развести вокальные фразы",10,"voice",false,false,true},
 {"dj_rhythm_echo","Ритмическая фраза в эхо",4,"rhythm",false,true},
 {"dj_hook_echo","Эхо мотива перед новым входом",4,"hook",false,true},
 {"dj_dance_build","Длинное развитие с повторителем",18,"build",true,false,true},
 {"dj_groove_loop","Удержание и передача грува",4,"loop",true,true},
 {"dj_harmonic_blur","Повтор и освобождение гармонии",8,"blur",true,true,true},
 {"dj_room_release","Раскрытие из реверберации",4,"release",false,false,true},
 {"dj_pulse_transition","Подведение к новому пульсу",4,"pulse",false,true,true}
}};
inline const DJStyle* djStyle(const std::string&id){for(const auto&s:djStyles)if(id==s.id)return &s;return nullptr;}
}
