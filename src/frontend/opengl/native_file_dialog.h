#ifndef TETR_OPENGL_NATIVE_FILE_DIALOG_H
#define TETR_OPENGL_NATIVE_FILE_DIALOG_H
#include "tetr/frontend/file_dialog.h"
#include "tetr/frontend/ui_surface.h"
#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <filesystem>
#include <fstream>
#endif

namespace Graphical {
inline std::optional<Frontend::UiCommand> file_browse_command(const Frontend::UiMetadata& m) {
    Frontend::UiCommand command{Frontend::UiCommandKind::BrowseFile,m.selected,{},m.page};
    if(m.kind==Frontend::UiKind::Text && m.parent) {
        command.index=m.parent->selected;command.ancestor=m.ancestors.size();
    }
    if(Frontend::valid_ui_command(m,command))return command;
    return {};
}
inline void SDLCALL file_dialog_complete(void* userdata,const char* const* files,int) {
    std::unique_ptr<std::shared_ptr<Frontend::FileDialog>> request(
        static_cast<std::shared_ptr<Frontend::FileDialog>*>(userdata));
    Frontend::FileDialogResult result;
    if(!files)result.error=SDL_GetError();
    else if(*files) {
        result.path=*files;
#ifdef __ANDROID__
        if(result.path.rfind("content://",0)==0) {
            // The shared replay reader expects a real path. SDL opens the
            // granted content URI; retain a private copy, never a fake path.
            SDL_IOStream* input=SDL_IOFromFile(result.path.c_str(),"rb");
            std::filesystem::path path;
            try {
                if(!input)throw std::runtime_error(SDL_GetError());
                std::filesystem::create_directories("imports");
                path=std::filesystem::path("imports")/("replay-"+std::to_string(SDL_GetTicksNS())+".txt");
                std::ofstream output(path,std::ios::binary);char bytes[65536];std::size_t total=0;
                for(;;) {
                    const auto count=SDL_ReadIO(input,bytes,sizeof(bytes));total+=count;
                    if(total>20000000)throw std::runtime_error("Replay exceeds 20 MB");
                    if(!count){if(SDL_GetIOStatus(input)==SDL_IO_STATUS_ERROR)throw std::runtime_error(SDL_GetError());break;}
                    output.write(bytes,count);if(!output)throw std::runtime_error("Could not import replay");
                }
                output.close();if(!output)throw std::runtime_error("Could not finish replay import");
                result.path=path.generic_u8string();
            }catch(const std::exception& e) {
                result.path.clear();result.error=e.what();std::error_code ignored;
                if(!path.empty())std::filesystem::remove(path,ignored);
            }
            if(input)SDL_CloseIO(input);
        }
#endif
    }
    (*request)->complete(std::move(result));
}
inline void show_file_dialog(SDL_Window* window,std::shared_ptr<Frontend::FileDialog> request,
    decltype(&SDL_ShowOpenFileDialog) show=SDL_ShowOpenFileDialog) {
    auto* callbackRequest=new std::shared_ptr<Frontend::FileDialog>(std::move(request));
    show(file_dialog_complete,callbackRequest,window,nullptr,0,(*callbackRequest)->directory.c_str(),false);
}
}
#endif
