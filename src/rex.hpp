#ifndef _rex_inc
#define _rex_inc
namespace _rex { struct re; }
class rex{
    _rex::re*r;
    rex(_rex::re*_r):r(_r){}
public:
    int pos=-1;
    unsigned len=0;
    rex(const char*pat,const char*n=0);
    static rex load(const char*n);
    bool match(const char*s,int l=-1,bool v=0);
    int matchbeg(const char*s);
    bool next(const char*s,int l=-1);
    void reset();
    bool first(const char*s,int l=-1);
    ~rex();
};
#endif