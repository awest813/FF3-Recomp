#include "guest_printf.hpp"
#include "psprecomp/common.hpp"
#include <algorithm>
#include <charconv>
#include <string_view>
namespace ff3 {
namespace {
constexpr std::size_t output_limit = 4096, field_limit = 1024;
std::string read_string(const psprecomp::GuestMemory &memory, std::uint32_t address,
                        std::size_t limit, bool require_terminator) {
    std::string result;
    for (std::size_t i=0; i<limit; ++i) {
        const auto at=static_cast<std::uint64_t>(address)+i;
        if (at>UINT32_MAX || !memory.contains(static_cast<std::uint32_t>(at)))
            throw psprecomp::Error("invalid guest string pointer");
        const auto ch=memory.load8(static_cast<std::uint32_t>(at));
        if (!ch) return result;
        result.push_back(static_cast<char>(ch));
    }
    if (require_terminator) throw psprecomp::Error("unterminated or oversized guest string");
    return result;
}
}
std::string format_guest_printf(const psprecomp::GuestMemory &memory, const psprecomp::AllegrexContext &ctx) {
    const auto format=read_string(memory, ctx.gpr[4], field_limit, true);
    unsigned argument=5;
    const auto next=[&]() {
        if (argument>11) throw psprecomp::Error("printf stack arguments not implemented");
        return ctx.gpr[argument++];
    };
    std::string output;
    const auto append=[&](const std::string &value) {
        if (output.size()+value.size()>output_limit) throw psprecomp::Error("printf output limit");
        output+=value;
    };
    for (std::size_t at=0; at<format.size();) {
        if (format[at]!='%') { append(std::string(1,format[at++])); continue; }
        ++at;
        if (at<format.size() && format[at]=='%') { append("%"); ++at; continue; }
        bool left=false, zero=false, plus=false, space=false, alternate=false;
        while (at<format.size()) {
            const char flag=format[at];
            if (flag=='-') left=true; else if (flag=='0') zero=true;
            else if (flag=='+') plus=true; else if (flag==' ') space=true;
            else if (flag=='#') alternate=true; else break;
            ++at;
        }
        const auto number=[&]() {
            std::size_t n=0;
            while (at<format.size() && format[at]>='0' && format[at]<='9') {
                n=n*10+static_cast<unsigned>(format[at++]-'0');
                if (n>field_limit) throw psprecomp::Error("printf field limit");
            }
            return static_cast<int>(n);
        };
        int width=0, precision=-1;
        if (at<format.size() && format[at]=='*') {
            ++at; const auto raw=static_cast<std::int32_t>(next());
            if (raw < -static_cast<int>(field_limit) || raw > static_cast<int>(field_limit))
                throw psprecomp::Error("printf field limit");
            left=left || raw<0; width=raw<0 ? -raw : raw;
        } else width=number();
        if (at<format.size() && format[at]=='.') {
            ++at;
            if (at<format.size() && format[at]=='*') {
                ++at; precision=static_cast<std::int32_t>(next());
                if (precision>static_cast<int>(field_limit)) throw psprecomp::Error("printf precision limit");
            } else precision=number();
        }
        // PSP long is 32 bits. Wider and floating-point arguments need separate ABI handling.
        const bool long_modifier = at<format.size() && format[at]=='l';
        if (long_modifier) ++at;
        if (at==format.size()) throw psprecomp::Error("incomplete printf conversion");
        const char conversion=format[at++];
        if (long_modifier && std::string_view("diuxXo").find(conversion)==std::string_view::npos)
            throw psprecomp::Error("unsupported printf length modifier");
        std::string value, prefix;
        bool numeric=false;
        if (conversion=='s') {
            value=read_string(memory, next(), precision<0 ? output_limit : static_cast<std::size_t>(precision), precision<0);
        } else if (conversion=='c') value.push_back(static_cast<char>(next()));
        else if (conversion=='d' || conversion=='i' || conversion=='u' || conversion=='x' || conversion=='X' || conversion=='o' || conversion=='p') {
            numeric=true;
            auto raw=next(); const bool nonzero=raw!=0;
            if (conversion=='d' || conversion=='i') {
                if (static_cast<std::int32_t>(raw)<0) { prefix="-"; raw=0u-raw; }
                else if (plus) prefix="+"; else if (space) prefix=" ";
            }
            const int base=(conversion=='x' || conversion=='X' || conversion=='p') ? 16 : conversion=='o' ? 8 : 10;
            char digits[32]; const auto result=std::to_chars(digits, digits+sizeof(digits), raw, base);
            value.assign(digits,result.ptr);
            if (!raw && precision==0) value.clear();
            if (conversion=='X') for (auto &ch:value) if (ch>='a' && ch<='f') ch=static_cast<char>(ch-'a'+'A');
            if (precision>static_cast<int>(value.size())) value.insert(0,precision-value.size(),'0');
            if (conversion=='p' || (alternate && nonzero && (conversion=='x' || conversion=='X')))
                prefix=conversion=='X' ? "0X" : "0x";
            if (alternate && conversion=='o' && (value.empty() || value[0]!='0')) prefix="0";
        } else throw psprecomp::Error("unsupported printf conversion");
        const auto length=prefix.size()+value.size();
        const auto padding=width>static_cast<int>(length) ? width-length : 0;
        if (left) append(prefix+value+std::string(padding,' '));
        else if (numeric && zero && precision<0) append(prefix+std::string(padding,'0')+value);
        else append(std::string(padding,' ')+prefix+value);
    }
    return output;
}
}
