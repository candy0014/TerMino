#include "terminal_viewport.h"
#include "tetr/util/terminal_text.h"
#include <algorithm>
#include <charconv>
#include <cstdio>

namespace Render {
void TerminalViewportBuffer::move(int row,int column) {
    // A positioned update or filename prompt can span an OS resize.
    if (const auto current=terminal_size()) size_=*current;
    row_=row; column_=column; lastVisible_=false;
    // Visible cursor-only updates matter during replay filename editing.
    if (row>=0 && row<size_.rows && column>=0 && column<size_.columns) {
        char position[48];
        const int length=std::snprintf(position,sizeof(position),"\x1b[%d;%dH",row+1,column+1);
        target_->sputn(position,length);
        physicalRow_=row; physicalColumn_=column;
    }
}
TerminalViewportBuffer::int_type TerminalViewportBuffer::overflow(int_type value) {
    if (!traits_type::eq_int_type(value,traits_type::eof())) consume(traits_type::to_char_type(value));
    return traits_type::not_eof(value);
}
std::streamsize TerminalViewportBuffer::xsputn(const char* text,std::streamsize count) {
    for (std::streamsize i=0;i<count;++i) consume(text[i]);
    return count;
}
void TerminalViewportBuffer::emit(std::string_view text,int width) {
    if (physicalRow_!=row_ || physicalColumn_!=column_) {
        char position[48];
        const int length=std::snprintf(position,sizeof(position),"\x1b[%d;%dH",row_+1,column_+1);
        target_->sputn(position,length);
    }
    target_->sputn(text.data(),static_cast<std::streamsize>(text.size()));
    physicalRow_=row_; physicalColumn_=column_+width;
}
void TerminalViewportBuffer::glyph() {
    const auto text=std::string_view(utf8_.data(),static_cast<std::size_t>(utf8Count_));
    const int width=static_cast<int>(Util::TerminalText::width(text));
    if (width==0) {
        if (lastVisible_) target_->sputn(text.data(),static_cast<std::streamsize>(text.size()));
    } else {
        const bool visible=row_>=0 && row_<size_.rows && column_>=0 && column_+width<=size_.columns;
        if (visible) emit(text,width);
        else if (row_>=0 && row_<size_.rows && width>1) {
            // Never send half a wide glyph. Blank just its visible cells.
            const int old=column_;
            for (int col=std::max(0,old);col<std::min(size_.columns,old+width);++col) {
                column_=col; emit(" ",1);
            }
            column_=old;
        }
        lastVisible_=visible;
        column_+=width;
    }
    utf8Count_=utf8Expected_=0;
}
void TerminalViewportBuffer::escape() {
    const char command=escape_.back();
    if (command=='H' || command=='f') {
        const auto text=std::string_view(escape_).substr(2,escape_.size()-3);
        const auto split=text.find(';');
        int row=1,col=1;
        const auto first=text.substr(0,split);
        std::from_chars(first.data(),first.data()+first.size(),row);
        if (split!=std::string_view::npos) {
            const auto second=text.substr(split+1);
            std::from_chars(second.data(),second.data()+second.size(),col);
        }
        move(row-1,col-1);
    } else if (command=='m' || command=='J' || command=='h' || command=='l') {
        target_->sputn(escape_.data(),static_cast<std::streamsize>(escape_.size()));
        if (command=='J') { physicalRow_=physicalColumn_=-1; lastVisible_=false; }
    }
    escape_.clear();
}
void TerminalViewportBuffer::consume(char value) {
    const auto byte=static_cast<unsigned char>(value);
    if (!escape_.empty()) {
        escape_+=value;
        if (escape_.size()==2 && value!='[') { escape_.clear(); return; }
        if (escape_.size()>2 && byte>='@' && byte<='~') escape();
        else if (escape_.size()>96) escape_.clear();
        return;
    }
    if (utf8Count_) {
        utf8_[static_cast<std::size_t>(utf8Count_++)]=value;
        if (utf8Count_==utf8Expected_) glyph();
        return;
    }
    if (value=='\x1b') { escape_=value; return; }
    // Positioned game drawing must never cause implicit wrapping or scrolling.
    if (value=='\n') { move(row_+1,0); return; }
    if (value=='\r') { move(row_,0); return; }
    if (value=='\b') { move(row_,column_-1); return; }
    if (byte<32 || byte==127) return;
    utf8Expected_=byte<0x80 ? 1 : byte<0xe0 ? 2 : byte<0xf0 ? 3 : 4;
    utf8_[0]=value; utf8Count_=1;
    if (utf8Expected_==1) glyph();
}
}
