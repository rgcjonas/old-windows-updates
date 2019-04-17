#pragma once

#include "dynarray.hpp"
#include "linereader.hpp"
#include "versioninfo.hpp"

class Worker {
    wstr m_caption;
    wstr m_text;
    dynarray<wstr> m_lines;

    Worker(const Worker &) { }
    Worker & operator=(const Worker &) { return *this; }

private:
    static wstr
    findKbNumber(const wstr &filename)
    {
        enum {
            STATE_SEP,
            STATE_K,
            STATE_B,
            STATE_NUM,
            STATE_OTHER
        } state = STATE_SEP;
        int kbstartpos = -1;
        int kbendpos = filename.size();

        for (int k = 0; k < filename.size(); ++k) {
            if (state == STATE_SEP) {
                if (filename[k] == '-' || filename[k] == '_' || filename[k] == ' ' || filename[k] == '.')
                    state = STATE_SEP;
                else if (filename[k] == 'K' || filename[k] == 'k')
                    state = STATE_K;
                else
                    state = STATE_OTHER;
            } else if (state == STATE_K) {
                if (filename[k] == 'b' || filename[k] == 'B')
                    state = STATE_B;
                else
                    state = STATE_OTHER;
            } else if (state == STATE_B) {
                if (filename[k] >= '0' && filename[k] <= '9') {
                    kbstartpos = k;
                    state = STATE_NUM;
                } else {
                    state = STATE_OTHER;
                }
            } else if (state == STATE_NUM) {
                if (filename[k] < '0' || filename[k] > '9') {
                    kbendpos = k;
                    break;
                }
            } else {
                if (filename[k] == '-' || filename[k] == '_' || filename[k] == ' ' || filename[k] == '.')
                    state = STATE_SEP;
                else
                    state = STATE_OTHER;
            }
        }

        if (kbstartpos >= 0) {
            return filename.substr(kbstartpos, kbendpos - kbstartpos);
        } else {
            return wstr();
        }
    }

public:
    struct Item {
        wstr text;
        wstr commandline;

        void
        process(HWND parent = NULL) const
        {
            PROCESS_INFORMATION pi;
            STARTUPINFO si;
            ZeroMemory(&pi, sizeof(pi));
            ZeroMemory(&si, sizeof(si));

            si.cb = sizeof(si);
            si.wShowWindow = SW_SHOWNOACTIVATE;
            si.dwFlags = STARTF_USESHOWWINDOW;

            if (CreateProcessW(NULL,
                               (WCHAR*)commandline.cstr(),
                               NULL, NULL,
                               FALSE,
                               0,
                               NULL,
                               NULL,
                               &si,
                               &pi)) {
                for (;;) {
                    DWORD wr = MsgWaitForMultipleObjects(1,
                                                         &pi.hProcess,
                                                         FALSE,
                                                         INFINITE,
                                                         QS_ALLINPUT);

                    MSG msg;
                    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) > 0) {
                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
                    }

                    if (wr == WAIT_OBJECT_0)
                        break;
                }

                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            } else {
                WCHAR *errormsg = NULL;
                FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                              NULL,
                              GetLastError(),
                              0,
                              (WCHAR*)&errormsg,
                              0,
                              NULL);
                MessageBox(parent, errormsg, L"Fehler beim Programmstart", MB_ICONHAND|MB_OK);
                LocalFree(errormsg);
            }
        }
    };

    Worker() : m_caption(L"Software-Installation"), m_text(L"Bitte warten Sie, w\x00E4hrend die Installation ausgef\x00FChrt wird. Dies kann einige Minuten dauern.")
    {
    }

    void load(LineReader &r)
    {
        wstr caption_prefix(L"!CAPTION=");
        wstr text_prefix(L"!TEXT=");

        while (!r.eof()) {
            wstr l = r.nextline().trimmed();

            if (l.size() == 0)
                continue;

            if (l[0] == ';')
                continue;

            if (l.startswith(caption_prefix)) {
                m_caption = l.substr(caption_prefix.size()).trimmed();
            } else if (l.startswith(text_prefix)) {
                m_text = l.substr(text_prefix.size()).trimmed();
            } else {
                m_lines.push(l);
            }
        }
    }

    wstr
    caption() const
    {
        return m_caption;
    }

    wstr
    text() const
    {
        return m_text;
    }

    int
    itemcount() const
    {
        return (int)m_lines.size();
    }

    Item
    prepareitem(int i) const
    {
        wstr file;
        wstr args;
        wstr description;

        wstr l = m_lines[i];

        // split line into parts
        int firstcolon = -1;
        int secondcolon = -1;
        for (int k = 0; k < l.size(); ++k) {
            if (l[k] == '\t') {
                firstcolon = k;
                break;
            }
        }

        if (firstcolon < 0) {
            file = l;
        } else {
            file = l.substr(0, firstcolon);
            for (int k = firstcolon+1; k < l.size(); ++k) {
                if (l[k] == '\t') {
                    secondcolon = k;
                    break;
                }
            }

            if (secondcolon < 0) {
                args = l.substr(firstcolon+1);
            } else {
                args = l.substr(firstcolon+1, secondcolon - firstcolon - 1);
                description = l.substr(secondcolon + 1);
            }
        }

        if (file.endswith(L".reg") || file.endswith(L".REG")) {
            // reg file
            args = args + wstr(L" /s \"") + file + wstr(L"\"");
            file = wstr(L"regedit.exe");
        } else if (file.endswith(L".msu") || file.endswith(L".MSU")) {
            if (description.size() < 1) {
                wstr kb = findKbNumber(file);
                if (kb.size() > 0) {
                    description = wstr(L"Update KB") + kb;
                } else {
                    description = file;
                }
            }

            if (!args.size()) {
                args = wstr(L"/quiet norestart");
            }

            args = wstr(L"\"") + file + wstr(L"\" ") + args;
            file = wstr(L"wusa.exe");
        } else if (file.endswith(L".msi") || file.endswith(L".MSI")) {
            if (description.size() < 1) {
                wstr kb = findKbNumber(file);
                if (kb.size() > 0) {
                    description = wstr(L"Update KB") + kb;
                } else {
                    description = file;
                }
            }

            if (!args.size()) {
                args = wstr(L"/qn /norestart");
            }

            args = wstr(L"/i \"") + file + wstr(L"\" ") + args;
            file = wstr(L"msiexec.exe");
        } else if (file.endswith(L".msp") || file.endswith(L".MSP")) {
            if (description.size() < 1) {
                wstr kb = findKbNumber(file);
                if (kb.size() > 0) {
                    description = wstr(L"Update KB") + kb;
                } else {
                    description = file;
                }
            }

            if (!args.size()) {
                args = wstr(L"/qn /norestart");
            }

            args = wstr(L"/p \"") + file + wstr(L"\" ") + args;
            file = wstr(L"msiexec.exe");
        } else {
            // default: EXE file

            // autodetect description and arguments
            if (args.size() < 1 || description.size() < 1) {
                // search for clues in version info
                VersionInfo vi(file.cstr());

                wstr kb = wstr(vi.query(L"KB Article Number"));
                wstr descr = wstr(vi.query(L"Package Type"));
                wstr filedesc = wstr(vi.query(L"FileDescription"));
                VS_FIXEDFILEINFO fixed = vi.queryFixedFileInfo();

                if (description.size() < 1 && kb.size() > 0) {
                    if (kb.startswith(L"KB") || kb.startswith(L"kb"))
                        kb = kb.substr(2);

                    if (!descr.size())
                        descr = wstr(L"Update");

                    description = descr + wstr(L" KB") + kb;
                }

                // search for KB number in filename
                if (description.size() < 1) {
                    wstr kb = findKbNumber(file);
                    if (kb.size() > 0) {
                        description = wstr(L"Update KB") + kb;
                    }
                }

                if (args.size() < 1) {
                    if (filedesc.startswith(L"Win32 Cabinet Self-Extractor")) {
                        args = wstr(L"/q:a /r:n");
                    } else if (fixed.dwProductVersionMS > 0x00060002 || (
                            fixed.dwProductVersionMS = 0x00060002 && fixed.dwProductVersionLS >= 0x00180000)) {
                        // new packages are supposed to work with the old-style arguments,
                        // but some (e.g. WMP11) will not.
                        args = wstr(L"/quiet /nobackup /norestart");
                    } else {
                        args = wstr(L"/q /n /z");
                    }
                }
            }
        }

        // fill output struct
        Item r;
        if (description.size())
            r.text = description;
        else
            r.text = file;

        r.commandline = wstr(L"\"") + file + wstr(L"\"");
        if (args.size())
            r.commandline = r.commandline + wstr(L" ") + args;

        return r;
    }
};
