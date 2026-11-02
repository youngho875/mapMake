#include "CopStyle.h"
#include <cstdint>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace mm 
{
    namespace 
    {
        uint32_t LE(const std::string& b,size_t o,int n) 
        {
            if(o+size_t(n)>b.size())
                throw std::runtime_error("Truncated BMP");

            uint32_t v=0;
            for(int i=0;i<n;++i)
                v|=uint32_t(uint8_t(b[o+i]))<<(i*8);
            return v;
        }
        void BE(std::string& s,uint32_t n)
        {
            for(int i=3;i>=0;--i)
                s+=char(n>>(i*8));
        }
        uint32_t Crc(const std::string& s)
        {
            uint32_t c=0xffffffffu;
            for(unsigned char b:s)
            {
                c^=b;
                for(int i=0;i<8;++i)
                    c=(c>>1)^(0xedb88320u&uint32_t(-int(c&1)));
            }
            return ~c;
        }
        void Chunk(std::string& png,const std::string& type,const std::string& data)
        {
            BE(png,static_cast<uint32_t>(data.size()));
            png+=type+data;BE(png,Crc(type+data));
        }
    }

    std::string BmpToPng(const fs::path& file,int key) 
    {
        std::ifstream in(file,std::ios::binary);
        if(!in)
            throw std::runtime_error("Cannot open symbol BMP");
        std::string b((std::istreambuf_iterator<char>(in)),{});

        if(b.size()<54||b.substr(0,2)!="BM"||LE(b,14,4)<40||LE(b,26,2)!=1||LE(b,28,2)!=24||LE(b,30,4)!=0)
            throw std::runtime_error("Symbol requires uncompressed 24-bit BMP (the supplied GSS files use this format)");
        int32_t w=static_cast<int32_t>(LE(b,18,4)),h=static_cast<int32_t>(LE(b,22,4));
        if(w<1||w>8192||h==0||h>8192||h< -8192)
            throw std::runtime_error("Invalid BMP dimensions");
        bool top=h<0;
        if(top)
            h=-h;

        size_t stride=(size_t(w)*3+3)&~size_t(3),offset=LE(b,10,4);
        if(offset<54||offset+stride*size_t(h)>b.size())
            throw std::runtime_error("Truncated BMP pixels");

        std::string raw;
        raw.reserve((size_t(w)*4+1)*h);
        for(int y=0;y<h;++y)
        {
            raw+='\0';
            for(int x=0;x<w;++x)
            {
                size_t pos=offset+stride*(top?y:h-1-y)+size_t(x)*3;
                unsigned char blue=b[pos],green=b[pos+1],red=b[pos+2];
                raw+=char(red);
                raw+=char(green);
                raw+=char(blue);
                raw+=char(((key==1&&red==255&&green==0&&blue==255)||(key==2&&red==255&&green==255&&blue==255))?0:255);
            }
        }
        // Standard PNG zlib stream using stored DEFLATE blocks; no external compression library.
        std::string z="\x78\x01";
        size_t pos=0;
        while(pos<raw.size())
        {
            size_t len=std::min<size_t>(65535,raw.size()-pos);
            z+=char(pos+len==raw.size()?1:0);
            z+=char(len);z+=char(len>>8);
            auto inv=uint16_t(~uint16_t(len));
            z+=char(inv);
            z+=char(inv>>8);
            z.append(raw,pos,len);
            pos+=len;
        }

        uint32_t a=1,c=0;
        for(unsigned char v:raw)
        {
            a=(a+v)%65521;
            c=(c+a)%65521;
        }
        BE(z,(c<<16)|a);
        std::string png="\x89PNG\r\n\x1a\n",ihdr;
        BE(ihdr,w);
        BE(ihdr,h);
        ihdr+=char(8);
        ihdr+=char(6);
        ihdr.append(3,'\0');
        Chunk(png,"IHDR",ihdr);
        Chunk(png,"IDAT",z);
        Chunk(png,"IEND","");
    
        return png;
    }
}
