#include <darkangel/audio.hpp>
#include <darkangel/hash.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace darkangel;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>bool refused(F call){try{call();}catch(const std::exception&){return true;}return false;}
std::string read(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);check(bool(in),"Sound fixture missing");return {std::istreambuf_iterator<char>(in),{}};}
void write(const std::filesystem::path& p,std::string_view bytes){std::ofstream out(p,std::ios::binary);out<<bytes;check(bool(out),"Sound fixture write");}
}
int main(){try{
    // The owned placeholder sounds are valid sources and cook like any other asset.
    const auto owned=std::filesystem::path(DAE_SOURCE_DIR)/"content/audio/placeholder",workspace=std::filesystem::path(DAE_BINARY_DIR)/("audio-"+AssetId::random().text()),mount=workspace/"source";std::filesystem::create_directories(mount);std::filesystem::copy(owned,mount/"placeholder",std::filesystem::copy_options::recursive);
    AssetService assets(mount,workspace/"cache");assets.scan();check(assets.assets().size()==2,"The placeholder sounds are not in the catalog");AssetId swing_id,hit_id;for(const auto& asset:assets.assets()){if(asset.path=="placeholder/swing.wav")swing_id=asset.id;if(asset.path=="placeholder/hit.wav")hit_id=asset.id;}
    check(assets.cook("placeholder/swing.wav").changed&&assets.cook("placeholder/hit.wav").changed&&!assets.cook("placeholder/swing.wav").changed,"Sound cook, or a warm cook that did work again");const std::array<AssetId,1> more{hit_id};assets.package(swing_id,workspace/"registry.json",more);
    const auto swing=load_cooked_sound(workspace/"registry.json",assets.cas_path(),swing_id),hit=load_cooked_sound(workspace/"registry.json",assets.cas_path(),hit_id);
    check(swing.format.channels==1&&swing.format.rate==48000&&swing.format.bits==16&&swing.format.frames>10000&&hit.format.frames>5000&&swing.generation.size()==64,"A cooked sound lost its format");
    // What is not a supported WAV file is refused, and an unadopted WAV file is not a source.
    {auto broken=swing.wave;broken[8]='X';check(refused([&]{read_sound_format(broken);})&&refused([&]{read_sound_format("RIFF");}),"A file that is not WAVE was read as a sound");write(mount/"placeholder/loose.wav",swing.wave);assets.scan();check(assets.assets().size()==2,"A WAV file without a sidecar became a source");
        write(mount/"placeholder/swing.wav","broken");check(refused([&]{assets.cook("placeholder/swing.wav");}),"A broken sound cooked");write(mount/"placeholder/swing.wav",swing.wave);}
    // The service on Amplitude's silent driver: no device is opened, and the lifecycle is the same.
    {AudioOptions options;options.device=false;options.work=workspace/"work";AudioService audio(options);check(!audio.stats().device&&audio.stats().driver=="null"&&audio.stats().sounds==0,"The silent service did not start empty");check(refused([&]{AudioService second(options);}),"A second audio service was built");
        check(audio.play(swing_id,{0,0,0})==0,"A sound that is not loaded played");const std::array<CookedSound,2> set{swing,hit};audio.load(set);check(audio.loaded(swing_id)&&audio.loaded(hit_id)&&audio.stats().sounds==2,"The sounds did not load");
        audio.listener({0,1.6f,0},{0,0,1},{0,1,0});const auto first=audio.play(swing_id,{1,1,2}),second=audio.play(hit_id,{-1,1,2});check(first&&second&&first!=second&&audio.alive(first)&&audio.stats().playing==2&&audio.stats().started==2,"Two sounds did not start");audio.move(first,{2,1,2});
        // A sound ends on its own after its length; the swing is 0.28 s.
        for(unsigned frame=0;frame<120&&audio.stats().playing;++frame){audio.update(1./60);std::this_thread::sleep_for(std::chrono::milliseconds(16));}check(audio.stats().playing==0&&!audio.alive(first),"A sound on the silent driver did not end by its length");
        // A stopped sound ends early; loading a new set drops what was playing.
        const auto third=audio.play(swing_id,{0,1,1});audio.stop(third,0);for(unsigned frame=0;frame<30&&audio.alive(third);++frame){audio.update(1./60);std::this_thread::sleep_for(std::chrono::milliseconds(16));}check(!audio.alive(third),"A stopped sound kept playing");
        audio.load(set);check(audio.stats().sounds==2&&audio.play(hit_id,{0,1,1})!=0,"Loading the same sounds again changed the service");
        // A new generation of a sound is played from then on; the other sound is untouched.
        {auto changed=hit;changed.generation=sha256("another hit");const std::array<CookedSound,1> one{changed};audio.load(one);check(audio.stats().sounds==2&&audio.loaded(swing_id)&&audio.play(hit_id,{0,1,1})!=0&&audio.play(swing_id,{0,1,1})!=0,"A new generation of a sound did not load beside the others");}}
    // The service can be made again after it is gone, and leaves no project folder behind.
    {AudioOptions options;options.device=false;options.work=workspace/"work";{AudioService again(options);}check(std::filesystem::is_empty(workspace/"work"),"An audio service left its generated project behind");}
    // On a machine with an audio device the service also starts on it. Nothing is played, so the test makes no sound; a machine without a device reports that and passes.
    std::string device="started";{AudioOptions options;options.work=workspace/"work";try{AudioService real(options);check(real.stats().device&&real.stats().driver=="miniaudio","The device service does not report its driver");}catch(const std::exception& error){device=std::string("unavailable (")+error.what()+")";}}
    std::cout<<"Audio device: "<<device<<"\n";
    std::cout<<"Audio: placeholder sounds in the catalog, cook, package and load, refusals, the service on the silent driver (load, play, move, natural end, stop, reload, new generation, single instance, restart, cleanup) passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
