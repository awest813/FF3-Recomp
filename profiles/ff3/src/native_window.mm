#import <AppKit/AppKit.h>
#include "native_window.hpp"
#include "native_audio.hpp"
#include "psprecomp/common.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <cmath>
#include <thread>
#include <iostream>

@interface FF3GameView : NSView <NSWindowDelegate> {
@public
    std::array<bool,128> keys;
    std::array<unsigned,128> pressed;
    bool closed;
    bool fast;
    bool muted;
    bool crisp;
    int volume;
    NSBitmapImageRep *bitmap;
}
@end
@implementation FF3GameView
- (BOOL)acceptsFirstResponder { return YES; }
- (void)keyDown:(NSEvent *)event {
    if (event.keyCode==48 && !event.isARepeat) fast=!fast;
    if (event.keyCode==3 && !event.isARepeat) { crisp=!crisp; [self setNeedsDisplay:YES]; }
    if (event.keyCode==27) volume=std::max(0,volume-10);
    if (event.keyCode==24) volume=std::min(100,volume+10);
    if (event.keyCode==46 && !event.isARepeat) muted=!muted;
    if (event.keyCode<keys.size()) {
        keys[event.keyCode]=true;
        // Preserve short taps when key down/up arrive in the same event batch.
        if (!event.isARepeat) pressed[event.keyCode]=3;
    }
}
- (void)keyUp:(NSEvent *)event {
    if (event.keyCode<keys.size()) keys[event.keyCode]=false;
}
- (void)windowDidResignKey:(NSNotification *)notification { keys.fill(false); pressed.fill(0); }
- (BOOL)windowShouldClose:(NSWindow *)sender { closed=true; return YES; }
- (void)drawRect:(NSRect)dirty {
    [[NSColor blackColor] setFill]; NSRectFill(self.bounds);
    if (!bitmap) return;
    const auto bounds=self.bounds;
    const double backing=self.window.backingScaleFactor;
    double scale=std::min(bounds.size.width/480.0,bounds.size.height/272.0);
    if(crisp && scale*backing>=1) scale=std::floor(scale*backing)/backing;
    NSRect target=NSMakeRect((bounds.size.width-480*scale)/2,
                            (bounds.size.height-272*scale)/2,480*scale,272*scale);
    target.origin.x=std::round(target.origin.x*backing)/backing;
    target.origin.y=std::round(target.origin.y*backing)/backing;
    const auto context=[NSGraphicsContext currentContext];
    const auto previous=context.imageInterpolation;
    context.imageInterpolation=crisp?NSImageInterpolationNone:NSImageInterpolationHigh;
    [bitmap drawInRect:target];
    context.imageInterpolation=previous;
}
@end

namespace ff3 {
struct NativeWindow::State {
    NSWindow *window;
    FF3GameView *view;
    std::uint64_t autoplay_until{};
    bool autoplay{};
    NativeAudio audio;
    std::chrono::steady_clock::time_point clock_origin{},measure_origin{};
    std::uint64_t guest_origin{},measure_guest{},presented{},changed{};
    double fps{},speed{};
    bool clock_ready{};
};

NativeWindow::NativeWindow(std::uint64_t autoplay_until_us):state_(std::make_unique<State>()) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];
        state_->window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,960,544)
            styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|
                      NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable
            backing:NSBackingStoreBuffered defer:NO];
        state_->window.releasedWhenClosed=NO;
        state_->window.contentMinSize=NSMakeSize(480,272);
        state_->window.contentAspectRatio=NSMakeSize(480,272);
        state_->view=[[FF3GameView alloc] initWithFrame:NSMakeRect(0,0,960,544)];
        state_->view->volume=100;
        state_->view->bitmap=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
            pixelsWide:480 pixelsHigh:272 bitsPerSample:8 samplesPerPixel:3
            hasAlpha:NO isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
            bytesPerRow:480*3 bitsPerPixel:24];
        if (!state_->view->bitmap) throw psprecomp::Error("Cannot allocate native display bitmap");
        std::memset(state_->view->bitmap.bitmapData,0,480*272*3);
        state_->window.contentView=state_->view;
        state_->window.delegate=state_->view;
        NSMenu *menu=[[NSMenu alloc] init];
        NSMenuItem *appItem=[[NSMenuItem alloc] init];
        [menu addItem:appItem];
        NSMenu *appMenu=[[NSMenu alloc] initWithTitle:@"FF3 Native"];
        NSMenuItem *quit=[[NSMenuItem alloc] initWithTitle:@"Quit FF3 Native"
            action:@selector(performClose:) keyEquivalent:@"q"];
        quit.target=state_->window;
        [appMenu addItem:quit];
        appItem.submenu=appMenu;
        NSApp.mainMenu=menu;
        state_->autoplay_until=autoplay_until_us;
        state_->window.title=@"Final Fantasy III — Native";
        state_->view.toolTip=@"Arrows: D-pad · WASD: analog · Z: confirm · X: back · C/V: square/triangle · Q/E: L/R · Return/Esc: Start · Backspace: Select · Tab: fast forward · M: mute · -/+: volume · F: smooth/pixel scaling";
        [state_->window center];
        [state_->window makeKeyAndOrderFront:nil];
        [state_->window makeFirstResponder:state_->view];
        [NSApp activateIgnoringOtherApps:YES];
    }
}
NativeWindow::~NativeWindow() {
    @autoreleasepool { state_->window.delegate=nil; [state_->window close]; }
}
bool NativeWindow::present(std::span<const std::uint8_t> rgb,std::uint64_t guest_us) {
    @autoreleasepool {
        auto &s=*state_;
        s.autoplay=guest_us<s.autoplay_until;
        for (auto &frames:s.view->pressed) if (frames) --frames;
        NSEvent *event;
        while ((event=[NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast]
                       inMode:NSDefaultRunLoopMode dequeue:YES])) [NSApp sendEvent:event];
        if (s.view->closed) return false;
        s.audio.volume(s.view->volume);
        s.audio.enable(!s.autoplay && !s.view->fast && !s.view->muted);
        if (!rgb.empty()) {
            if (rgb.size()!=480*272*3) throw psprecomp::Error("Unexpected native framebuffer size");
            if(std::memcmp(s.view->bitmap.bitmapData,rgb.data(),rgb.size())) {
                std::memcpy(s.view->bitmap.bitmapData,rgb.data(),rgb.size());
                [s.view setNeedsDisplay:YES];++s.changed;
            }
        }
        NSString *title=s.autoplay?@"Final Fantasy III — Auto-starting New Game…":
            s.view->fast?@"Final Fantasy III — Fast forward (Tab to end)":
            [NSString stringWithFormat:@"Final Fantasy III — %.0f fps · %.0f%% speed · %@ · %d%% audio%@ · Z: confirm",s.fps,s.speed,s.view->crisp?@"Pixel":@"Smooth",s.view->volume,s.view->muted?@" · Muted":@""];
        if (![s.window.title isEqualToString:title]) s.window.title=title;
        [s.window displayIfNeeded];
        [NSApp updateWindows];
        const auto now=std::chrono::steady_clock::now();
        if (s.autoplay || s.view->fast) s.clock_ready=false;
        else {
            if(!s.clock_ready) {s.clock_origin=now;s.guest_origin=guest_us;s.clock_ready=true;}
            auto deadline=s.clock_origin+std::chrono::microseconds(guest_us-s.guest_origin);
            if(deadline<now-std::chrono::milliseconds(250)) {
                // A modal/load stall must not trigger a burst of catch-up frames.
                s.clock_origin=now;s.guest_origin=guest_us;deadline=now;
                s.audio.enable(false);s.audio.enable(!s.view->muted);
            }
            std::this_thread::sleep_until(deadline);
        }
        ++s.presented;
        if(s.measure_origin.time_since_epoch().count()==0) {s.measure_origin=now;s.measure_guest=guest_us;}
        const double elapsed=std::chrono::duration<double>(now-s.measure_origin).count();
        if(elapsed>=2.0) {
            s.fps=s.changed/elapsed;s.speed=(guest_us-s.measure_guest)/(elapsed*10000.0);
            if(std::getenv("FF3_PERF")) {
                const auto audio=s.audio.stats();
                std::cerr<<"[perf] present_hz="<<s.presented/elapsed<<" changed_fps="<<s.fps
                         <<" guest_speed="<<s.speed<<" audio_played="<<audio.played
                         <<" audio_empty="<<audio.empty<<" audio_dropped="<<audio.dropped
                         <<" audio_nonzero="<<audio.nonzero<<"\n";
            }
            s.measure_origin=now;s.measure_guest=guest_us;s.presented=s.changed=0;
        }
        return true;
    }
}
void NativeWindow::audio(unsigned channel,std::span<const std::int16_t> pcm) {state_->audio.submit(channel,pcm);}
void NativeWindow::clear_audio(unsigned channel) {state_->audio.clear(channel);}
BootstrapHle::ControllerSample NativeWindow::controller() const {
    const auto &s=*state_;
    BootstrapHle::ControllerSample sample;
    if (s.autoplay || !s.window.isKeyWindow) return sample;
    const auto down=[&](int key){return s.view->keys[key] || s.view->pressed[key];};
    // Physical macOS key codes make bindings independent of text input/IME.
    for (const auto [key,mask]:std::array<std::pair<int,std::uint32_t>,13>{{
         {126,0x10},{124,0x20},{125,0x40},{123,0x80},
         {6,0x4000},{7,0x2000},{8,0x8000},{9,0x1000},
         {12,0x100},{14,0x200},{36,0x8},{53,0x8},{51,0x1}}})
        if (down(key)) sample.buttons|=mask;
    sample.x=down(0)==down(2)?128:down(0)?0:255;
    sample.y=down(13)==down(1)?128:down(13)?0:255;
    return sample;
}
std::optional<std::size_t> NativeWindow::save_slot(const std::vector<SaveSlot> &slots, bool saving) {
    @autoreleasepool {
        NSAlert *alert=[[NSAlert alloc] init];
        alert.messageText=saving?@"Final Fantasy III — Save":@"Final Fantasy III — Load";
        alert.informativeText=@"Choose a save slot.";
        NSPopUpButton *picker=[[NSPopUpButton alloc] initWithFrame:NSMakeRect(0,0,300,28) pullsDown:NO];
        for (const auto &slot:slots) {
            NSString *name=[NSString stringWithUTF8String:slot.name.c_str()];
            [picker addItemWithTitle:[NSString stringWithFormat:@"%@ — %@",name,slot.occupied?@"Saved game":@"Empty"]];
        }
        alert.accessoryView=picker;
        [alert addButtonWithTitle:saving?@"Save":@"Load"];
        [alert addButtonWithTitle:@"Cancel"];
        state_->view->keys.fill(false);state_->view->pressed.fill(0);
        state_->audio.enable(false);
        const auto result=[alert runModal];
        state_->clock_ready=false;
        if (result!=NSAlertFirstButtonReturn) return std::nullopt;
        const auto index=static_cast<std::size_t>(picker.indexOfSelectedItem);
        if (index>=slots.size()) return std::nullopt;
        if (saving && slots[index].occupied) {
            NSAlert *confirm=[[NSAlert alloc] init];
            confirm.messageText=@"Replace this saved game?";
            confirm.informativeText=[NSString stringWithUTF8String:slots[index].name.c_str()];
            [confirm addButtonWithTitle:@"Replace"];[confirm addButtonWithTitle:@"Cancel"];
            const auto overwrite=[confirm runModal];
            state_->clock_ready=false;
            if (overwrite!=NSAlertFirstButtonReturn) return std::nullopt;
        }
        return index;
    }
}
std::uint32_t NativeWindow::dialog(const std::string &text,std::uint32_t options) {
    @autoreleasepool {
        NSAlert *alert=[[NSAlert alloc] init];
        alert.messageText=@"Final Fantasy III";
        alert.informativeText=[NSString stringWithUTF8String:text.c_str()]?:@"Game system message";
        [alert addButtonWithTitle:(options&0x10)?@"Yes":@"OK"];
        if (options&0x10) [alert addButtonWithTitle:@"No"];
        [alert addButtonWithTitle:@"Back"];
        state_->view->keys.fill(false);
        state_->view->pressed.fill(0);
        state_->audio.enable(false);
        const auto result=[alert runModal];
        state_->clock_ready=false;
        if (result==NSAlertFirstButtonReturn) return 1;
        if ((options&0x10) && result==NSAlertSecondButtonReturn) return 2;
        return 3;
    }
}
std::optional<std::u16string> NativeWindow::keyboard(const std::u16string &description,const std::u16string &initial,std::uint32_t limit) {
    @autoreleasepool {
        NSAlert *alert=[[NSAlert alloc] init];
        alert.messageText=@"Final Fantasy III — Name";
        alert.informativeText=[[NSString alloc] initWithCharacters:
            reinterpret_cast<const unichar *>(description.data()) length:description.size()];
        NSTextField *field=[[NSTextField alloc] initWithFrame:NSMakeRect(0,0,280,26)];
        field.stringValue=[[NSString alloc] initWithCharacters:
            reinterpret_cast<const unichar *>(initial.data()) length:initial.size()];
        alert.accessoryView=field;
        [alert addButtonWithTitle:@"OK"];[alert addButtonWithTitle:@"Cancel"];
        state_->view->keys.fill(false);state_->view->pressed.fill(0);
        state_->audio.enable(false);
        for (;;) {
            [alert.window setInitialFirstResponder:field];
            const auto result=[alert runModal];
            state_->clock_ready=false;
            if (result!=NSAlertFirstButtonReturn) return std::nullopt;
            NSString *value=field.stringValue;
            if (value.length>limit) {
                alert.informativeText=[NSString stringWithFormat:@"Use at most %u characters.",limit];
                continue;
            }
            std::u16string text(value.length,u'\0');
            [value getCharacters:reinterpret_cast<unichar *>(text.data()) range:NSMakeRange(0,value.length)];
            return text;
        }
    }
}
}
