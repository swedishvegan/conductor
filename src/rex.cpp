#include "rex.hpp"
#include <string>
#include <iostream>
#include <cstring>
#include <cassert>
#include <unordered_map>
#include <memory>
namespace _rex {
    struct re{enum class op{c,a,k,o,cc,cs};op t;bool m=0,e=0;re(op x):t(x){}virtual~re()=default;virtual void init(bool,bool)=0;virtual void step(char,bool,bool)=0;virtual re* cp()const=0;};
    struct rc:re{re*l,*r;rc(re*a,re*b):re(op::c),l(a),r(b){e=l->e&r->e;}~rc(){delete l;delete r;}void init(bool s,bool v)override{re*p=v?r:l,*q=v?l:r;p->init(s,v);q->init(p->m,v);m=q->m;}void step(char c,bool s,bool v)override{re*p=v?r:l,*q=v?l:r;bool q_=p->m|(s&p->e);p->step(c,s,v);q->step(c,q_,v);m=q->m|(p->m&q->e);}re*cp()const override{return new rc(l->cp(),r->cp());}};
    struct ra:re{re*l,*r;ra(re*a,re*b):re(op::a),l(a),r(b){e=l->e|r->e;}~ra(){delete l;delete r;}void init(bool s,bool v)override{l->init(s,v);r->init(s,v);m=l->m|r->m;}void step(char c,bool s,bool v)override{l->step(c,s,v);r->step(c,s,v);m=l->m|r->m;}re*cp()const override{return new ra(l->cp(),r->cp());}};
    struct rk:re{re*r;explicit rk(re*n,bool _e):re(op::k),r(n){e=_e;}~rk(){delete r;}void init(bool s,bool v)override{r->init(s,v);m=e?s:0;}void step(char c,bool s,bool v)override{r->step(c,s|m,v);m=r->m;}re*cp()const override{return new rk(r->cp(),e);}};
    struct ro:re{re*r;explicit ro(re*n):re(op::o),r(n){e=1;}~ro(){delete r;}void init(bool s,bool v)override{r->init(s,v);m=s;}void step(char c,bool s,bool v)override{r->step(c,s,v);m=r->m;}re*cp()const override{return new ro(r->cp());}};
    struct rcc:re{uint8_t s,h;rcc(uint8_t a,uint8_t b):re(op::cc),s(a),h(b){}void init(bool,bool)override{m=0;}void step(char c,bool st,bool)override{m=st&&uint8_t(c)>=s&&uint8_t(c)<=h;}re*cp()const override{return new rcc(s,h);}};
    struct rcs:re{char*p;uint64_t x=0;uint8_t n;bool w=0;rcs(const char*s,uint8_t l):re(op::cs),p((char*)s),n(l){if(l<2){e=!l;return;}uint8_t i=0;for(;i<l;++i)if(s[i]=='\\')break;if(i==l)return;p=new char[l];w=1;n=0;bool b=0;for(i=0;i<l;++i){if(!b&&s[i]=='\\'){b=1;continue;}p[n++]=s[i];b=0;}assert(n<=64);e=!n;}~rcs(){if(w)delete[]p;}void init(bool,bool)override{m=0;x=0;}void step(char c,bool st,bool v)override{if(e)return;if(!n){m=st;return;}m=0;x|=st;uint64_t b=1ULL<<(n-1),l=b;if((x&b)&&c==p[v?0:n-1])m=1;x&=~b;for(int i=n-2;i>=0;--i){b=1ULL<<i;if((x&b)&&c==p[v?n-i-1:i])x|=l;x&=~b;l=b;}}re*cp()const override{return new rcs(p,n);}};
}
using namespace _rex;
std::unordered_map<std::string,std::unique_ptr<re>> rg;
re*lx(char c){return new rcc((uint8_t)c,(uint8_t)c);}
bool il(char c){return c=='*'||c=='+'||c=='?';}
bool im(char c){return c=='('||c==')'||c=='{'||c=='}'||il(c)||c=='-';}
re* f(const char*,int,int);
re*ml(re*r,char c){return c=='?'?static_cast<re*>(new ro(r)):static_cast<re*>(new rk(r,c=='*'));}
re*sp(const char*p,int s,int e,re*r,int i,int j,int n){if(n>0)r=new rc(f(p,s,j),r);if(i+1<e)r=new rc(r,f(p,i+1,e));return r;}
re*al(const char*p,int e,re*r,int&i){if(i+1<e&&il(p[i+1]))r=ml(r,p[++i]);return r;}static re*ld(std::string n){auto it=rg.find(n);assert(it!=rg.end());return it->second->cp();}
re*f(const char*p,int s,int e){if(e<0)e=uint16_t(strlen(p));if(s>=e||(s+1==e&&p[s]=='\\'))return new rcs("",0);assert(!il(p[s])&&p[s]!='-');if(s+1==e){assert(!im(p[s]));return lx(p[s]);}int par=0;bool bs=0;for(int i=s;i<e;++i){char ch=p[i];if(bs){bs=0;continue;}if(ch=='(')++par;else if(ch==')')--par;else if(ch=='|'&&par==0){assert(i!=s&&i!=e-1);return new ra(f(p,s,i),f(p,i+1,e));}else if(ch=='\\')bs=1;}par=0;bs=0;char cl=0;int ps=0,n=0;for(int i=s;i<e;++i){char ch=p[i];if(!bs&&ch=='\\'){bs=1;if(i==e-1)e--;continue;}if(bs){bs=0;cl=ch;++n;continue;}if(par==0){if(ch=='{'){int j=i;while(p[i]!='}')++i,assert(i<e);re*_r=ld(std::string(p+j+1,i-j-1));_r=al(p,e,_r,i);return sp(p,s,e,_r,i,j,n);}if(ch=='-'){int j=i;char chn=p[++i];assert(cl&&i<e&&!im(chn));if(chn=='\\')chn=p[++i];auto lo=(uint8_t)cl,hi=(uint8_t)chn;assert(lo<=hi);re*_r=al(p,e,new rcc(lo,hi),i);return sp(p,s,e,_r,i,j-1,n-1);}if(il(ch)){assert(cl);return sp(p,s,e,ml(lx(cl),ch),i,i-1,n-1);}if(ch=='.'){int j=i;re*_r=al(p,e,new rcc(1,255),i);return sp(p,s,e,_r,i,j,n);}}cl=0;if(ch==')'){assert(par>0);if(--par==0){re*_r=al(p,e,f(p,ps+1,i),i);return sp(p,s,e,_r,i,ps,n);}}else if(ch=='('){if(par==0)ps=i;++par;}else if(par==0){cl=ch;++n;}}assert(par==0);if(n>1)return new rcs(p+s,e-s);return p[s]=='\\'?lx(p[s+1]):lx(p[s]);}
rex::rex(const char*pat,const char*n){r=f(pat,0,-1);if(n){std::string s(n);assert(rg.find(s)==rg.end());rg[s]=std::unique_ptr<re>(r->cp());}}
rex rex::load(const char*n){return rex(ld(std::string(n)));}
rex::~rex(){delete r;}
bool rex::match(const char*s,int l,bool v){r->init(1,v);for(int i=0;s[i]&&(l<0||i<l);++i)r->step(s[i],i==0,v);return r->m;}
int rex::matchbeg(const char*s){r->init(1,0); if(r->m)return 0;for(int i=0;s[i];++i){r->step(s[i],i==0,0);if(r->m)return i+1;}return -1;}
bool rex::next(const char*s,int l){r->init(1,1);int st=pos>=0?pos+len:0,a=-1,b=0,i=st;if(l<0)while(s[i])++i;else i=l;if(r->m)a=i;for(--i;i>=st;--i){r->step(s[i],1,1);if(r->m)a=i;}if(a<0){pos=-1;len=0;return 0;}r->init(1,0);if(r->m)b=a-1;for(i=a;s[i]&&(l<0||i<l);++i){r->step(s[i],i==a,0);if(r->m)b=i;}pos=a;len=1+(b-a);return 1;}
void rex::reset(){pos=-1;len=0;}
bool rex::first(const char*s,int l){reset();return next(s,l);}