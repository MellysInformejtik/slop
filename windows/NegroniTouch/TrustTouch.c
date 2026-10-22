/* Install the packaged test certificate inside WinPE, which lacks certutil. */
#include <windows.h>
#include <wincrypt.h>

static void say(const char *s) {
    DWORD n=0, written;
    while(s[n]) ++n;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),s,n,&written,0);
}
void entry(void) {
    WCHAR path[MAX_PATH];
    static BYTE der[16384];
    DWORD size=GetModuleFileNameW(0,path,MAX_PATH),got=0;
    if(!size || size>=MAX_PATH) ExitProcess(1);
    while(size && path[size-1]!=L'\\') --size;
    const WCHAR name[]=L"NegroniTouch.cer";
    if(!size || size+sizeof(name)/sizeof(WCHAR)>MAX_PATH) ExitProcess(2);
    for(DWORD i=0;i<sizeof(name)/sizeof(WCHAR);++i) path[size+i]=name[i];
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
    if(f==INVALID_HANDLE_VALUE) {say("Touch certificate missing\r\n");ExitProcess(3);}
    DWORD bytes=GetFileSize(f,0);
    BOOL ok=bytes>0 && bytes<=sizeof(der) && ReadFile(f,der,bytes,&got,0) && got==bytes;
    CloseHandle(f);
    if(!ok) {say("Cannot read touch certificate\r\n");ExitProcess(4);}
    const WCHAR *names[]={L"ROOT",L"TrustedPublisher"};
    for(unsigned i=0;i<2;++i) {
        HCERTSTORE store=CertOpenStore(CERT_STORE_PROV_SYSTEM_W,0,0,
            CERT_SYSTEM_STORE_LOCAL_MACHINE,names[i]);
        if(!store) {say("Cannot open PE certificate store\r\n");ExitProcess(5);}
        ok=CertAddEncodedCertificateToStore(store,X509_ASN_ENCODING,der,bytes,
            CERT_STORE_ADD_REPLACE_EXISTING,0);
        CertCloseStore(store,0);
        if(!ok) {say("Cannot trust touch certificate\r\n");ExitProcess(6);}
    }
    say("Touch test certificate installed in PE ROOT and TrustedPublisher\r\n");
    ExitProcess(0);
}
