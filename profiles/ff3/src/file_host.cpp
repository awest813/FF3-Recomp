#include "file_host.hpp"
#include "host_system.hpp"
#include "psprecomp/common.hpp"
#include <algorithm>
#include <iostream>
#include <cstdlib>
namespace ff3 {
FileHost::FileHost(std::filesystem::path disc):disc_(std::filesystem::weakly_canonical(disc)),memory_stick_(disc_.parent_path()/"ms0"),iso_(std::getenv("FF3_ISO_PATH")?std::getenv("FF3_ISO_PATH"):(disc_.parent_path()/"disc.iso").string()) {}
FileHost::~FileHost(){for(auto &[id,file]:files_)if(file.stream)std::fclose(file.stream);}
std::filesystem::path FileHost::path(const std::string &guest,bool write) const {
    auto name=guest;std::replace(name.begin(),name.end(),'\\','/');
    const auto colon=name.find(':');const auto device=colon==std::string::npos?"disc0":name.substr(0,colon);
    if(colon!=std::string::npos)name.erase(0,colon+1);
    if(device!="disc0"&&device!="umd0"&&device!="ms0")throw psprecomp::Error("Unmapped PSP device: "+device);
    if(write&&device!="ms0")throw psprecomp::Error("Disc writes are unsupported");
    while(!name.empty()&&name.front()=='/')name.erase(name.begin());
    std::filesystem::path relative(name);
    for(const auto &part:relative)if(part=="..")throw psprecomp::Error("Guest path escapes device root");
    if(colon==std::string::npos && device=="disc0")relative=std::filesystem::path("PSP_GAME/USRDIR")/relative;
    const auto root=device=="ms0"?memory_stick_:disc_;
    const auto resolved=std::filesystem::weakly_canonical(root/relative);
    const auto check=std::filesystem::relative(resolved,root);
    for(const auto &part:check)if(part=="..")throw psprecomp::Error("Guest symlink escapes device root");
    return resolved;
}
bool FileHost::stat(psprecomp::Runtime &rt,std::uint32_t out,const std::filesystem::path &file){
    if(!rt.memory().contains(out,88))throw psprecomp::Error("Invalid stat buffer");
    if(!std::filesystem::exists(file))return false;
    const bool directory=std::filesystem::is_directory(file);const auto size=directory?0:std::filesystem::file_size(file);
    rt.memory().zero(out,88);rt.memory().store32(out,(directory?0x1000:0x2000)|0777);rt.memory().store32(out+4,directory?0x10:0x20);
    rt.memory().store32(out+8,static_cast<std::uint32_t>(size));rt.memory().store32(out+12,static_cast<std::uint32_t>(size>>32));
    // Stable synthetic timestamps for locally extracted read-only assets.
    for(auto at:{16u,32u,48u}){rt.memory().store16(out+at,2000);rt.memory().store16(out+at+2,1);rt.memory().store16(out+at+4,1);}return true;
}
void FileHost::install(psprecomp::Runtime &runtime){
    runtime.register_hle("sceUmdUser",0xAEE7404Du,[this](auto &rt,auto &ctx){
        umd_callback_=ctx.gpr[4];const auto caller=ctx;ctx.gpr[5]=umd_state_;rt.invoke_import("ThreadManForUser",0xC11BA8C4u,ctx);ctx=caller;ctx.set_gpr(2,0);
    });
    runtime.register_hle("sceUmdUser",0xBD2BDE07u,[this](auto &,auto &ctx){if(umd_callback_==ctx.gpr[4])umd_callback_=0;ctx.set_gpr(2,0);});
    runtime.register_hle("sceUmdUser",0x46EBB729u,[this](auto &,auto &ctx){ctx.set_gpr(2,std::filesystem::is_directory(disc_)?1:0);});
    runtime.register_hle("sceUmdUser",0x6B4A146Cu,[this](auto &,auto &ctx){ctx.set_gpr(2,umd_state_);});
    runtime.register_hle("sceUmdUser",0x20628E6Fu,[](auto &,auto &ctx){ctx.set_gpr(2,0);});
    runtime.register_hle("sceUmdUser",0xC6183D47u,[this](auto &rt,auto &ctx){
        if(ctx.gpr[4]!=1 || rt.memory().read_c_string(ctx.gpr[5],16)!="disc0:" || !std::filesystem::is_directory(disc_)){rt.stop("UMD activation needs local disc assets");return;}umd_state_=0x32;ctx.set_gpr(2,0);
    });
    for(auto nid:{0x87533940u,0xCBE9F02Au})runtime.register_hle("sceUmdUser",nid,[this,nid](auto &,auto &ctx){umd_replace_allowed_=nid==0xCBE9F02Au;ctx.set_gpr(2,0);});
    for(auto nid:{0x8EF08FCEu,0x4A9E5E29u,0x56202973u})runtime.register_hle("sceUmdUser",nid,[this](auto &rt,auto &ctx){if(!(ctx.gpr[4]&umd_state_)){rt.stop("UMD wait for unavailable drive state");return;}ctx.set_gpr(2,0);});
    runtime.register_hle("IoFileMgrForUser",0x54F5FB11u,[this](auto &rt,auto &ctx){
        const auto device=rt.memory().read_c_string(ctx.gpr[4],64);const auto command=ctx.gpr[5];
        if (device=="fatms0:" && (command==0x02415821 || command==0x02415822)) {
            if(ctx.gpr[7]!=4 || !rt.memory().contains(ctx.gpr[6],4)){rt.stop("Invalid memory stick callback pointer");return;}
            const auto cb=rt.memory().load32(ctx.gpr[6]);
            if(command==0x02415821){stick_callbacks_.insert(cb);const auto caller=ctx;ctx.gpr[4]=cb;ctx.gpr[5]=1;rt.invoke_import("ThreadManForUser",0xC11BA8C4u,ctx);ctx=caller;}
            else stick_callbacks_.erase(cb);
            ctx.set_gpr(2,0);return;
        }
        if((device=="mscmhc0:" && command==0x02025806) || (device=="fatms0:" && command==0x02425823)) {
            if(ctx.gpr[9]<4 || !rt.memory().contains(ctx.gpr[8],4)){rt.stop("Invalid memory stick state output");return;}rt.memory().store32(ctx.gpr[8],1);ctx.set_gpr(2,0);return;
        }
        rt.stop("Unsupported devctl "+device+" "+psprecomp::hex32(command));
    });
    runtime.register_hle("IoFileMgrForUser",0xB29DDF9Cu,[this](auto &rt,auto &ctx){
        const auto dir=path(rt.memory().read_c_string(ctx.gpr[4],1024));if(!std::filesystem::is_directory(dir)){ctx.set_gpr(2,0x80010002);return;}
        Directory record;for(const auto &entry:std::filesystem::directory_iterator(dir))record.entries.push_back(entry.path());std::sort(record.entries.begin(),record.entries.end());const auto fd=next_fd_++;directories_.emplace(fd,std::move(record));ctx.set_gpr(2,fd);
    });
    runtime.register_hle("IoFileMgrForUser",0xE3EB004Cu,[this](auto &rt,auto &ctx){
        const auto it=directories_.find(ctx.gpr[4]);if(it==directories_.end()){ctx.set_gpr(2,0x80010009);return;}auto &dir=it->second;
        if(dir.cursor==dir.entries.size()){ctx.set_gpr(2,0);return;}const auto out=ctx.gpr[5];if(!rt.memory().contains(out,352)){rt.stop("Invalid directory output");return;}
        const auto &entry=dir.entries[dir.cursor++];rt.memory().zero(out,352);stat(rt,out,entry);const auto name=entry.filename().string();if(name.size()>255){rt.stop("Directory name too long");return;}
        for(std::size_t i=0;i<name.size();++i)rt.memory().store8(out+88+i,name[i]);ctx.set_gpr(2,1);
    });
    runtime.register_hle("IoFileMgrForUser",0xEB092469u,[this](auto &,auto &ctx){ctx.set_gpr(2,directories_.erase(ctx.gpr[4])?0:0x80010009);});
    const auto open=[this](auto &rt,auto &ctx,bool async){
        const auto name=rt.memory().read_c_string(ctx.gpr[4],1024); const auto flags=ctx.gpr[5];
        const bool write=(flags&3)!=1;
        unsigned long long sector=0,length=0;int consumed=0;
        if(name.starts_with("disc0:/sce_lbn")) {
            if(write || std::sscanf(name.c_str(),"disc0:/sce_lbn0x%llx_size0x%llx%n",&sector,&length,&consumed)!=2 || consumed!=name.size() || !std::filesystem::exists(iso_) || sector>UINT64_MAX/2048){rt.stop("Invalid raw sector path or missing disc.iso");return;}
            const auto base=static_cast<std::uint64_t>(sector)*2048;const auto iso_size=std::filesystem::file_size(iso_);
            if(base>iso_size || length>iso_size-base){rt.stop("Raw sector read outside ISO");return;}
            auto stream=open_file(iso_,"rb");if(!stream || seek_file(stream,static_cast<std::int64_t>(base),SEEK_SET)){if(stream)std::fclose(stream);rt.stop("Cannot open raw ISO sectors");return;}
            const auto fd=next_fd_++;files_.emplace(fd,File{stream,static_cast<std::int64_t>(fd),base,length});ctx.set_gpr(2,fd);std::cout<<"[io] open "<<name<<" ok (ISO sectors)\n";return;
        }
        const auto file=path(name,write);
        const char *mode=write?((flags&0x400)?"wb+":(flags&0x100)?"ab+":"rb+"):"rb";
        if(write&&(flags&0x200))std::filesystem::create_directories(file.parent_path());
        auto stream=open_file(file,mode);
        if(!stream&&write&&(flags&0x200))stream=open_file(file,"wb+");
        std::cout<<"[io] open "<<name<<(stream?" ok":" missing")<<'\n';
        if(!stream){ctx.set_gpr(2,0x80010002u);return;}
        const auto fd=next_fd_++;files_.emplace(fd,File{stream,static_cast<std::int64_t>(fd),0,std::filesystem::file_size(file)});ctx.set_gpr(2,fd);(void)async;
    };
    runtime.register_hle("IoFileMgrForUser",0x109F50BCu,[open](auto &r,auto &c){open(r,c,false);});
    runtime.register_hle("IoFileMgrForUser",0x89AA9906u,[open](auto &r,auto &c){open(r,c,true);});
    const auto read=[this](auto &rt,auto &ctx,bool async){
        const auto it=files_.find(ctx.gpr[4]);const auto out=ctx.gpr[5],count=ctx.gpr[6];
        if(it==files_.end()||!it->second.stream){ctx.set_gpr(2,0x80010009u);return;}
        if(!rt.memory().contains(out,count)){rt.stop("Invalid file read buffer");return;}
        std::vector<std::uint8_t> data(count);const auto &file=it->second;const auto pos=static_cast<std::uint64_t>(tell_file(file.stream))-file.base;const auto available=pos>=file.length?0:file.length-pos;const auto n=std::fread(data.data(),1,std::min<std::uint64_t>(count,available),file.stream);data.resize(n);rt.memory().copy_in(out,data);
        const auto result=std::ferror(it->second.stream)?static_cast<std::int32_t>(0x80010005u):static_cast<std::int64_t>(n);
        it->second.result=result;ctx.set_gpr(2,async?0:static_cast<std::uint32_t>(result));
    };
    runtime.register_hle("IoFileMgrForUser",0x6A638D83u,[read](auto &r,auto &c){read(r,c,false);});
    runtime.register_hle("IoFileMgrForUser",0xA0B5A7C2u,[read](auto &r,auto &c){read(r,c,true);});
    const auto close=[this](auto &,auto &ctx,bool async){const auto it=files_.find(ctx.gpr[4]);if(it==files_.end()||!it->second.stream){ctx.set_gpr(2,0x80010009u);return;}std::fclose(it->second.stream);if(async){it->second.stream=nullptr;it->second.result=0;}else files_.erase(it);ctx.set_gpr(2,0);};
    runtime.register_hle("IoFileMgrForUser",0x810C4BC3u,[close](auto &r,auto &c){close(r,c,false);});
    runtime.register_hle("IoFileMgrForUser",0xFF5940B6u,[close](auto &r,auto &c){close(r,c,true);});
    const auto seek=[this](auto &,auto &ctx,bool async,bool wide){
        const auto it=files_.find(ctx.gpr[4]);if(it==files_.end()||!it->second.stream){ctx.set_gpr(2,0x80010009u);return;}
        const auto offset=wide?static_cast<std::int64_t>((static_cast<std::uint64_t>(ctx.gpr[7])<<32)|ctx.gpr[6]):static_cast<std::int32_t>(ctx.gpr[5]);
        const auto whence=wide?ctx.gpr[8]:ctx.gpr[6];
        auto &file=it->second;const auto origin=whence==0?0:whence==1?static_cast<std::int64_t>(tell_file(file.stream))-static_cast<std::int64_t>(file.base):static_cast<std::int64_t>(file.length);
        const auto position=origin+offset;
        const auto result=(whence>2 || position<0 || seek_file(file.stream,static_cast<std::int64_t>(file.base)+position,SEEK_SET))?static_cast<std::int64_t>(static_cast<std::int32_t>(0x80010016u)):position;
        it->second.result=result;ctx.set_gpr(2,async?0:static_cast<std::uint32_t>(result));if(wide&&!async)ctx.set_gpr(3,static_cast<std::uint32_t>(static_cast<std::uint64_t>(result)>>32));
    };
    runtime.register_hle("IoFileMgrForUser",0x27EB27B8u,[seek](auto &r,auto &c){seek(r,c,false,true);});
    runtime.register_hle("IoFileMgrForUser",0x71B19E77u,[seek](auto &r,auto &c){seek(r,c,true,true);});
    runtime.register_hle("IoFileMgrForUser",0x68963324u,[seek](auto &r,auto &c){seek(r,c,false,false);});
    const auto wait=[this](auto &rt,auto &ctx,bool stat){const auto it=files_.find(ctx.gpr[4]);const auto out=stat?ctx.gpr[6]:ctx.gpr[5];if(it==files_.end()){ctx.set_gpr(2,0x80010009u);return;}if(!rt.memory().contains(out,8)){rt.stop("Invalid async IO result");return;}const auto value=static_cast<std::uint64_t>(it->second.result);rt.memory().store32(out,static_cast<std::uint32_t>(value));rt.memory().store32(out+4,static_cast<std::uint32_t>(value>>32));ctx.set_gpr(2,0);if(!it->second.stream)files_.erase(it);};
    for(auto nid:{0xE23EEC33u,0x35DBD746u,0x3251EA56u})runtime.register_hle("IoFileMgrForUser",nid,[wait](auto &r,auto &c){wait(r,c,false);});
    runtime.register_hle("IoFileMgrForUser",0xCB05F8D6u,[wait](auto &r,auto &c){wait(r,c,true);});
    runtime.register_hle("IoFileMgrForUser",0xACE946E8u,[this](auto &rt,auto &ctx){ctx.set_gpr(2,stat(rt,ctx.gpr[5],path(rt.memory().read_c_string(ctx.gpr[4],1024)))?0:0x80010002u);});
    runtime.register_hle("IoFileMgrForUser",0x42EC03ACu,[this](auto &rt,auto &ctx){const auto count=ctx.gpr[6];if(!rt.memory().contains(ctx.gpr[5],count)){rt.stop("Invalid file write buffer");return;}std::vector<std::uint8_t> bytes(count);rt.memory().copy_out(ctx.gpr[5],bytes);if(ctx.gpr[4]==1||ctx.gpr[4]==2){std::cout.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());ctx.set_gpr(2,count);return;}const auto it=files_.find(ctx.gpr[4]);if(it==files_.end()||!it->second.stream){ctx.set_gpr(2,0x80010009u);return;}ctx.set_gpr(2,static_cast<std::uint32_t>(std::fwrite(bytes.data(),1,count,it->second.stream)));});
}
}
