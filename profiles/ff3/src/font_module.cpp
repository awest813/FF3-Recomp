#include "font_module.hpp"
#include "font_identity.hpp"
#include "psprecomp/sha256.hpp"
#include "psprecomp/common.hpp"
#include <iostream>
namespace ff3_font {void register_generated_functions(psprecomp::Runtime &);}
namespace ff3 {
void FontModule::install(psprecomp::Runtime &rt,BootstrapHle &hle) {
    if(!hle.reserve_static(0x09F00000,0x10000))throw psprecomp::Error("Cannot reserve font module address range");
    rt.register_hle("sceReg",0x92E41280u,[this,&hle](auto &r,auto &c){
        const auto p=c.gpr[4];if(!r.memory().contains(p,272)||r.memory().load32(p)!=1||c.gpr[5]!=2||!r.memory().contains(c.gpr[6],4)){r.stop("Unsupported font registry open");return;}
        const auto name=r.memory().read_c_string(p+4,256);if(!name.empty()&&name!="/system"){r.stop("Unknown font registry");return;}
        const auto id=hle.allocate_uid();registries_.emplace(id,name);r.memory().store32(c.gpr[6],id);c.set_gpr(2,0);
    });
    rt.register_hle("sceReg",0xFA8A5739u,[this](auto &,auto &c){c.set_gpr(2,registries_.erase(c.gpr[4])?0:0x80082718);});
    rt.register_hle("sceReg",0x1D8A762Eu,[this,&hle](auto &r,auto &c){
        const auto name=r.memory().read_c_string(c.gpr[5],256);
        if(!registries_.contains(c.gpr[4])||!r.memory().contains(c.gpr[7],4)){r.stop("Invalid registry category request");return;}
        if(name!="/DATA/FONT"&&name!="/SYSPROFILE/RESOLUTION"){r.stop("Unknown font registry category: "+name);return;}
        const auto id=hle.allocate_uid();categories_.emplace(id,name);r.memory().store32(c.gpr[7],id);c.set_gpr(2,0);
    });
    rt.register_hle("sceReg",0x0CAE832Bu,[this](auto &,auto &c){c.set_gpr(2,categories_.erase(c.gpr[4])?0:0x80082718);});
    rt.register_hle("sceReg",0xD4475AA8u,[this,&hle](auto &r,auto &c){
        const auto category=categories_.find(c.gpr[4]);const auto key=r.memory().read_c_string(c.gpr[5],256);
        if(category==categories_.end()){r.stop("Unknown font category handle");return;}
        std::vector<std::uint8_t> data;std::uint32_t type=2,value=0;
        if(category->second=="/DATA/FONT" && key=="path_name") {const std::string path="flash0:/font/";data.assign(path.begin(),path.end());data.push_back(0);type=3;}
        else if(category->second=="/DATA/FONT" && key=="num_fonts")value=0; // No firmware font files are mounted.
        else if(category->second=="/SYSPROFILE/RESOLUTION" && (key=="horizontal"||key=="vertical"))value=72*64;
        else {r.stop("Unknown font registry key: "+key);return;}
        if(type==2)for(unsigned i=0;i<4;++i)data.push_back(value>>(i*8));
        const auto id=hle.allocate_uid();r.memory().store32(c.gpr[6],id);r.memory().store32(c.gpr[7],type);r.memory().store32(c.gpr[8],data.size());keys_.emplace(id,std::move(data));c.set_gpr(2,0);
    });
    rt.register_hle("sceReg",0x28A8E98Au,[this](auto &r,auto &c){
        const auto key=keys_.find(c.gpr[5]);if(!categories_.contains(c.gpr[4])||key==keys_.end()||c.gpr[7]<key->second.size()){r.stop("Invalid font registry value request");return;}
        r.memory().copy_in(c.gpr[6],key->second);c.set_gpr(2,0);
    });
    rt.register_hle("ModuleMgrForUser",0x977DE386u,[this,&hle](auto &runtime,auto &ctx){
        const auto name=runtime.memory().read_c_string(ctx.gpr[4],1024);
        if(name=="disc0:/PSP_GAME/USRDIR/FILES/MODULE/psmf.prx"||name=="disc0:/PSP_GAME/USRDIR/FILES/MODULE/libpsmfplayer.prx"){
            const auto id=hle.allocate_uid();media_modules_.emplace(id,name);ctx.set_gpr(2,id);std::cout<<"[module] HLE media adapter "<<name<<" uid="<<id<<'\n';return;
        }
        if(name!="disc0:/PSP_GAME/USRDIR/FILES/MODULE/libfont.prx" || ctx.gpr[5] || ctx.gpr[6]) {runtime.stop("Unsupported module load: "+name);return;}
        if(uid_){ctx.set_gpr(2,uid_);return;}
        if(psprecomp::sha256_file(image_)!=ff3_font::sha256)throw psprecomp::Error("Prepared font module hash mismatch");
        auto elf=psprecomp::Elf32Image::from_file(image_);elf.load_into(runtime.memory());
        const auto module=elf.find_module_info(runtime.memory());if(!module)throw psprecomp::Error("Font module info missing");
        if(elf.entry()!=UINT32_MAX)throw psprecomp::Error("Unexpected font module entry point");
        ff3_font::register_generated_functions(runtime);
        for(const auto &entry:elf.scan_exports(runtime.memory(),*module)) {
            if(entry.variable || entry.library=="syslib")continue;
            if(!runtime.has_function(entry.address))throw psprecomp::Error("Font export missing from generated corpus");
            runtime.register_hle(entry.library,entry.nid,[this,address=entry.address,gp=module->gp](auto &r,auto &c){
                if(!started_){r.stop("Font module called before start");return;}
                callers_[psprecomp::runtime_thread_uid()].push_back({c.gpr[31],c.gpr[28]});
                c.gpr[31]=0x08000010;c.gpr[28]=gp;c.pc=address;
            });
        }
        uid_=hle.allocate_uid();ctx.set_gpr(2,uid_);std::cout<<"[module] native sceFont_Library loaded at 0x09F00000 uid="<<uid_<<'\n';
    });
    rt.register_hle("ModuleMgrForUser",0x50F0C1ECu,[this](auto &r,auto &c){
        if(media_modules_.contains(c.gpr[4])){if(c.gpr[5]||c.gpr[8]){r.stop("Unsupported media module start options");return;}if(c.gpr[7])r.memory().store32(c.gpr[7],0);c.set_gpr(2,c.gpr[4]);return;}
        if(c.gpr[4]!=static_cast<std::uint32_t>(uid_) || !uid_ || c.gpr[5] || c.gpr[8]){r.stop("Unsupported font module start");return;}
        if(c.gpr[7])r.memory().store32(c.gpr[7],0);started_=true;c.set_gpr(2,uid_);
    });
    rt.register_hle("ModuleMgrForUser",0xD1FF982A,[this](auto &r,auto &c){
        if(!media_modules_.contains(c.gpr[4])){r.stop("Unsupported module stop");return;}
        if(c.gpr[5]||c.gpr[8]){r.stop("Unsupported module stop options");return;}
        if(c.gpr[7])r.memory().store32(c.gpr[7],0);c.set_gpr(2,0);
    });
    rt.register_hle("ModuleMgrForUser",0x2E0911AA,[this](auto &,auto &c){c.set_gpr(2,media_modules_.erase(c.gpr[4])?0:0x8002012E);});
    rt.register_hle("ff3Font",0,[this](auto &r,auto &c){
        auto &stack=callers_[psprecomp::runtime_thread_uid()];if(stack.empty()){r.stop("Font return without caller");return;}
        const auto caller=stack.back();stack.pop_back();c.gpr[31]=caller.ra;c.gpr[28]=caller.gp;c.pc=caller.ra;
    });
    rt.register_function(0x08000010,[](auto &r,auto &c){r.invoke_import("ff3Font",0,c);},"ff3_font_return");
}
}
