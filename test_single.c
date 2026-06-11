/* Quick single-method tests */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
static HANDLE w,r;
static void snd(const char *m){int l=strlen(m);char h[128];int hl=snprintf(h,sizeof(h),"Content-Length: %d\r\n\r\n",l);DWORD dw;WriteFile(w,h,hl,&dw,NULL);WriteFile(w,m,l,&dw,NULL);}
static char *rd(void){char b[4096];DWORD t=0;while(t<sizeof(b)-1){DWORD n;if(!ReadFile(r,b+t,1,&n,NULL)||n==0)return NULL;t++;if(t>=4&&b[t-4]=='\r'&&b[t-3]=='\n'&&b[t-2]=='\r'&&b[t-1]=='\n')break;}b[t]=0;int cl=0;const char *p=strstr(b,"Content-Length:");if(p)cl=atoi(p+15);if(cl<=0)return NULL;char *bd=malloc(cl+1);DWORD rt=0;while(rt<(DWORD)cl){DWORD n;ReadFile(r,bd+rt,cl-rt,&n,NULL);rt+=n;}bd[cl]=0;return bd;}
int main(int argc, char **argv){
    if(argc<2){printf("Usage: test_single <method>\n");return 1;}
    HANDLE ri,wo;SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};
    CreatePipe(&ri,&w,&sa,0);CreatePipe(&r,&wo,&sa,0);
    SetHandleInformation(w,HANDLE_FLAG_INHERIT,0);SetHandleInformation(r,HANDLE_FLAG_INHERIT,0);
    PROCESS_INFORMATION pi={0};STARTUPINFOA si={0};si.cb=sizeof(si);
    si.hStdInput=ri;si.hStdOutput=wo;si.hStdError=GetStdHandle(STD_ERROR_HANDLE);si.dwFlags=STARTF_USESTDHANDLES;
    CreateProcessA(NULL,"lxclua-lsp.exe",NULL,NULL,TRUE,0,NULL,NULL,&si,&pi);
    CloseHandle(ri);CloseHandle(wo);Sleep(100);
    char *resp;
    
    /* Init */
    snd("{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"initialize\",\"params\":{\"processId\":1,\"rootUri\":\"file:///t\",\"capabilities\":{}}}");
    resp=rd();free(resp);
    snd("{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}");
    
    /* Open a test doc */
    snd("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"file:///t/m.lua\",\"languageId\":\"lxclua\",\"version\":1,\"text\":\"function f()\\n  if true then\\n    print(1)\\n  end\\nend\\nstruct Point x,y end\"}}}");
    
    /* Test the requested method */
    char req[2048];
    if(strcmp(argv[1],"docSymbol")==0)
        snprintf(req,sizeof(req),"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"textDocument/documentSymbol\",\"params\":{\"textDocument\":{\"uri\":\"file:///t/m.lua\"}}}");
    else if(strcmp(argv[1],"folding")==0)
        snprintf(req,sizeof(req),"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"textDocument/foldingRange\",\"params\":{\"textDocument\":{\"uri\":\"file:///t/m.lua\"}}}");
    else if(strcmp(argv[1],"semantic")==0)
        snprintf(req,sizeof(req),"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"textDocument/semanticTokens/full\",\"params\":{\"textDocument\":{\"uri\":\"file:///t/m.lua\"}}}");
    else if(strcmp(argv[1],"codeAction")==0)
        snprintf(req,sizeof(req),"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"textDocument/codeAction\",\"params\":{\"textDocument\":{\"uri\":\"file:///t/m.lua\"},\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":1}},\"context\":{\"diagnostics\":[]}}}");
    else if(strcmp(argv[1],"formatting")==0)
        snprintf(req,sizeof(req),"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"textDocument/formatting\",\"params\":{\"textDocument\":{\"uri\":\"file:///t/m.lua\"},\"options\":{\"tabSize\":4,\"insertSpaces\":true}}}");
    else if(strcmp(argv[1],"diagnostic")==0)
        snprintf(req,sizeof(req),"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"textDocument/diagnostic\",\"params\":{\"textDocument\":{\"uri\":\"file:///t/m.lua\"}}}");
    else {printf("Unknown method\n");return 1;}
    
    printf("Request: %s\n",argv[1]);
    snd(req);
    resp=rd();
    if(resp){
        printf("Response: %.400s\n",resp);
        /* Check for errors */
        if(strstr(resp,"-32601")) printf("  => Method not found!\n");
        else if(strstr(resp,"\"error\"")) printf("  => Error response\n");
        else if(strstr(resp,"\"result\"")) printf("  => Success!\n");
        free(resp);
    } else {
        printf("  => NULL (no response / timeout)\n");
    }
    
    /* Cleanup */
    snd("{\"jsonrpc\":\"2.0\",\"id\":99,\"method\":\"shutdown\",\"params\":{}}");
    resp=rd();free(resp);
    snd("{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":{}}");
    WaitForSingleObject(pi.hProcess,2000);
    CloseHandle(w);CloseHandle(r);CloseHandle(pi.hProcess);CloseHandle(pi.hThread);
    return 0;
}