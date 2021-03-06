// UpdateHandler.cpp: implementation of the CUpdateHandler class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "wippien.h"
#include "UpdateHandler.h"
#include "../CxImage/zlib/zlib.h"
#include "ProgressDlg.h"
#include "BaloonTip.h"
#include "Buffer.h"
#include "Settings.h"
#include "MainDlg.h"
#include "crypto_AES.h"
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>


#ifndef _APPUPDLIB
_ATL_FUNC_INFO INFO_CloseApp = {CC_STDCALL, VT_EMPTY, 0, 0};
_ATL_FUNC_INFO INFO_CheckDone = {CC_STDCALL, VT_EMPTY, 3, {VT_I4, VT_I4, VT_BSTR}};
_ATL_FUNC_INFO INFO_StateChange = {CC_STDCALL, VT_EMPTY, 1, {VT_I4}};
_ATL_FUNC_INFO INFO_UpdateDone = {CC_STDCALL, VT_EMPTY, 2, {VT_I4, VT_BSTR}};
_ATL_FUNC_INFO INFO_FileStart = {CC_STDCALL, VT_EMPTY, 1, {VT_DISPATCH}};
_ATL_FUNC_INFO INFO_FileProgress = {CC_STDCALL, VT_EMPTY, 3, {VT_DISPATCH, VT_I4, VT_I4}};
_ATL_FUNC_INFO INFO_FileDone = {CC_STDCALL, VT_EMPTY, 3, {VT_DISPATCH, VT_I4, VT_BSTR}};
_ATL_FUNC_INFO INFO_DownloadDone = {CC_STDCALL, VT_EMPTY, 2, {VT_I4, VT_BSTR}};
_ATL_FUNC_INFO INFO_PrevDetected = {CC_STDCALL, VT_EMPTY, 0, 0};
#endif

extern CSettings _Settings;
extern CMainDlg _MainDlg;
int uuencode(unsigned char *src, unsigned int srclength,char *target, size_t targsize);
BOOL m_UpdateHandlerMsgBoxShown = FALSE;
CUpdateHandler *_UpdateHandler;

DWORD WINAPI ShowMessageThreadProc(void *d)
{
	Buffer *data = (Buffer *)d;

	char *text = data->Ptr();
	char *caption = text;
	caption+= strlen(text);
	caption++;

	m_UpdateHandlerMsgBoxShown = TRUE;
	CBalloonTipDlg::Show(NULL, text, caption, MB_OK);
	m_UpdateHandlerMsgBoxShown = FALSE;
	delete data;
	return 0;
}

#ifndef _APPUPDLIB
class wodAppUpdateEvents: public IDispEventSimpleImpl<1, wodAppUpdateEvents, &DIID__IwodAppUpdateComEvents> 
{
public:

	wodAppUpdateEvents(CUpdateHandler *Owner)
	{
		m_Owner = Owner;
		DispEventAdvise ( (IUnknown*)Owner->m_Update);
	}

	virtual ~wodAppUpdateEvents ()
	{
		DispEventUnadvise ( (IUnknown*)m_Owner->m_Update);
//		m_Owner->m_Update.Release();
	}
#endif

#ifndef _APPUPDLIB
	void _stdcall CloseApp()
#else
	void AppUpd_CloseApp(void *wodAppUpd)
#endif
	{
		if (::IsWindow(_MainDlg.m_hWnd))
		{
			::PostMessage(_MainDlg.m_hWnd, WM_COMMAND, ID_EXIT, 0);
		}
	}
#ifndef _APPUPDLIB
	void _stdcall CheckDone(long NewFiles, long ErrorCode, BSTR ErrorText)
	{
#else
	void AppUpd_CheckDone(void *wodAppUpd, long NewFiles, long ErrorCode, char *ErrorText)
	{
		VARIANT tag;
		WODAPPUPDCOMLib::AppUpd_GetTag(wodAppUpd, &tag);
		CUpdateHandler *m_Owner = (CUpdateHandler *)tag.plVal;
#endif
		if (!ErrorCode)
		{	
			short count = 0;
#ifndef _APPUPDLIB
			IUpdMessages *msgs = NULL;
			if (SUCCEEDED(m_Owner->m_Update->get_Messages(&msgs)))
			{
				if (SUCCEEDED(msgs->get_Count(&count)))
				{
					IUpdMessage *msg = NULL, *showmsg = NULL;
#else
					void *msg = NULL, *showmsg = NULL;
					WODAPPUPDCOMLib::AppUpd_Messages_GetCount(wodAppUpd, &count);
#endif
					short min = 32000;
					for (int i=0;i<count && !showmsg;i++)
					{
#ifndef _APPUPDLIB
						if (SUCCEEDED(msgs->get_Item(i, &msg)))
#else
						if (!WODAPPUPDCOMLib::AppUpd_Messages_GetMessage(wodAppUpd, i, &msg) && msg)

#endif
						{

							long ID = 0;
#ifndef _APPUPDLIB
							msg->get_ID(&ID);
#else
							WODAPPUPDCOMLib::AppUpd_Message_GetID(msg, &ID);
#endif

							if (ID < min && ID>_Settings.m_LastOperatorMessageID)
							{
								min = (short)ID;
								if (showmsg)
#ifndef _APPUPDLIB
									showmsg->Release();
#else
									WODAPPUPDCOMLib::AppUpd_Messages_Free(showmsg);
#endif
								showmsg = msg;
							}
							else
#ifndef _APPUPDLIB
								msg->Release();
#else
							WODAPPUPDCOMLib::AppUpd_Messages_Free(msg);
#endif

						}
					}	
					
					if (showmsg && _Settings.m_ShowUpdaterMessages)
					{
						if (!m_UpdateHandlerMsgBoxShown)
						{
							long ID;
#ifndef _APPUPDLIB
							showmsg->get_ID(&ID);
#else
							WODAPPUPDCOMLib::AppUpd_Message_GetID(showmsg, &ID);
#endif
							_Settings.m_LastOperatorMessageID = ID;
							_Settings.SaveConfig();

							// spawn new thread to show the message
#ifndef _APPUPDLIB
							CComBSTR2 text, caption;
							showmsg->get_Text(&text);
							showmsg->get_Caption(&caption);
							showmsg->Release();
#else
							char text[32768] = {0}, caption[32768] = {0};
							int len = sizeof(text);
							WODAPPUPDCOMLib::AppUpd_Message_GetText(msg, text, &len);
							len = sizeof(caption);
							WODAPPUPDCOMLib::AppUpd_Message_GetCaption(msg, caption, &len);
							WODAPPUPDCOMLib::AppUpd_Messages_Free(showmsg);
#endif

							Buffer *b = new Buffer;
#ifndef _APPUPDLIB
							b->Append(text.ToString());
							b->Append("\0",1);
							b->Append(caption.ToString());
							b->Append("\0",1);
#else
							b->Append(text);
							b->Append("\0",1);
							b->Append(caption);
							b->Append("\0",1);
#endif
							DWORD id = 0;
							HANDLE h = CreateThread(NULL, 0, ShowMessageThreadProc, b, 0, &id);
							CloseHandle(h);
						}
#ifndef _APPUPDLIB
					}
				}
				msgs->Release();
#endif
			}

			// loop through files, and if those are languages, update ours
			m_Owner->m_NewLanguageFiles = 0;
//			if (NewFiles)
			Buffer AvailableLangFiles;
			{

				// let's enumerate what we have so far
				AvailableLangFiles.Append("\r\n");
				char buff[32768];
				strcpy(buff, _Settings.m_LanguagePath);
				strcat(buff, "*.txt");
				
				WIN32_FIND_DATA FileData;
				
				HANDLE hSearch = FindFirstFile(buff, &FileData); 
				CComBSTR2 Author, Version;
				
				if (hSearch != INVALID_HANDLE_VALUE) 
				{ 
					BOOL fFinished = FALSE;
					while (!fFinished) 
					{ 
						char *a = strstr(FileData.cFileName, ".txt");
						if (a)
							*a = 0;
						AvailableLangFiles.Append(FileData.cFileName);
						AvailableLangFiles.Append("\r\n");
						if (!FindNextFile(hSearch, &FileData)) 
							fFinished = TRUE; 
					} 
					
					// Close the search handle. 		
					FindClose(hSearch);
				}

				short cnt = 0;
				WODAPPUPDCOMLib::AppUpd_Files_GetCount(wodAppUpd, &cnt);
				for (int i=0;i<cnt;i++)
				{
					void *file = NULL;
					WODAPPUPDCOMLib::AppUpd_Files_GetFile(wodAppUpd, i, &file);
					if (file)
					{
						char path[1024];
						int plen = sizeof(path);

						WODAPPUPDCOMLib::AppUpd_File_GetPath(file, path, &plen);
						if (plen>0)
						{
							if (strstr(path, "\\Language"))
							{
								// yes, this is language.. 
								// get version
								int newver = 0;
								plen = sizeof(path);
								WODAPPUPDCOMLib::AppUpd_File_GetNewVersion(file, path, &plen);
								if (plen>0)
								{
									newver = atoi(path);
								}
								if (newver>0)
								{
									BOOL needreplace = FALSE;
									WODAPPUPDCOMLib::AppUpd_File_GetNeedReplace(file, &needreplace);
									plen = sizeof(path);
									WODAPPUPDCOMLib::AppUpd_File_GetName(file, path, &plen);
									if (plen>0)
									{
										// is it english or our file?
										char *a = strchr(path, '.');
										if (a)
											*a = 0;
										CComBSTR2 l = _Settings.m_Language;
										if (!strcmp(path, "English")) // check if english
										{
											if (newver>_Settings.m_LanguageEngFileVersion)
											{
												m_Owner->m_NewLanguageFiles++;
												if (!needreplace) // did wodappupdate detected new version?
												{
													WODAPPUPDCOMLib::AppUpd_File_SetNeedReplace(file, TRUE);
													NewFiles++;
												}
											}
											else
											{
												if (needreplace)
												{
													WODAPPUPDCOMLib::AppUpd_File_SetNeedReplace(file, FALSE);
													NewFiles--;
												}
											}
										}
										else
										if (!strcmp(path, l.ToString()) && strcmp(l.ToString(), "English")) // check if our langauge
										{
											if (newver>_Settings.m_LanguageFileVersion)
											{
												m_Owner->m_NewLanguageFiles++;
												if (!needreplace) // did wodappupdate detected new version?
												{
													WODAPPUPDCOMLib::AppUpd_File_SetNeedReplace(file, TRUE);
													NewFiles++;
												}
											}
											else
											{
												if (needreplace)
												{
													WODAPPUPDCOMLib::AppUpd_File_SetNeedReplace(file, FALSE);
													NewFiles--;
												}
											}
										}
										else
										{
											// if we don't have this locally, we must download it
											strcat(path, "\r\n");
											if (!strstr(AvailableLangFiles.Ptr(), path))
											{
												m_Owner->m_NewLanguageFiles++;
												// we don't have it locally, fetch it
												if (!needreplace)
												{
													WODAPPUPDCOMLib::AppUpd_File_SetNeedReplace(file, TRUE);
													NewFiles++;
												}
											}
											else // we don't need it at all. we have it but ignore it
											if (needreplace)
											{
												// don't download!
												WODAPPUPDCOMLib::AppUpd_File_SetNeedReplace(file, FALSE);
												NewFiles--;
											}
										}
									}
								}
							}
						}
						WODAPPUPDCOMLib::AppUpd_Files_Free(file);
					}
				}
			}
			if (NewFiles > 0)
			{
				int answer = IDNO;
#ifndef _APPUPDLIB
				m_Owner->m_Update->put_Visible(m_Owner->m_Silently?VARIANT_FALSE:VARIANT_TRUE);
#else
				WODAPPUPDCOMLib::AppUpd_SetVisible(wodAppUpd, m_Owner->m_Silently?VARIANT_FALSE:VARIANT_TRUE);
#endif
				if (m_Owner->m_SilentCheck && m_Owner->m_Silently)
					answer = IDYES;
				else
				{
					if (m_UpdateHandlerMsgBoxShown)
						return;

					m_UpdateHandlerMsgBoxShown = TRUE;
					CComBSTR nv = "<font face=Verdana size=2>";
					if (m_Owner->m_NewLanguageFiles == NewFiles)
						nv += _Settings.Translate("New language files available. Download?");
					else
						nv += _Settings.Translate("New version of Wippien is available. Download?");
					nv += "<br>\r\n<br>\r\n";
					if (m_Owner->m_NewLanguageFiles != NewFiles)
					{
						nv += "<a href=\"http://www.wippien.com/notes.php\">";
						nv += _Settings.Translate("Click here to see what's new.");
						nv += "</a>";
					}
					CComBSTR2 nv2 = nv;
					answer = CBalloonTipDlg::Show(NULL, nv2.ToString(), _Settings.Translate("Wippien update"), MB_YESNO);
					m_UpdateHandlerMsgBoxShown = FALSE;

//					m_UpdateHandlerMsgBoxShown = TRUE;
//					answer = MessageBox(NULL, "New version of Wippien is available. Download?\r\n\r\nClick on Help to see what's new.", "Wippien update", MB_YESNO | MB_HELP | MB_ICONQUESTION);
//					m_UpdateHandlerMsgBoxShown = FALSE;
				}

				if (answer == IDYES)
				{
#ifndef _APPUPDLIB
					m_Owner->m_Update->put_Visible(m_Owner->m_Silently?VARIANT_FALSE:VARIANT_TRUE);
					m_Owner->m_Update->Download();
#else
					WODAPPUPDCOMLib::AppUpd_SetVisible(wodAppUpd, m_Owner->m_Silently?VARIANT_FALSE:VARIANT_TRUE);
					WODAPPUPDCOMLib::AppUpd_Download(wodAppUpd);
#endif
				}

			}
			else
			{
				if (!m_Owner->m_SilentCheck)
				{
					if (m_UpdateHandlerMsgBoxShown)
						return;
					m_UpdateHandlerMsgBoxShown = TRUE;
					MessageBox(NULL, _Settings.Translate("Your version of Wippien is up-to-date."), _Settings.Translate("Wippien update"), MB_OK);
					m_UpdateHandlerMsgBoxShown = FALSE;
				}
			}
		}
		else
			if (!m_Owner->m_SilentCheck)
			{
				if (m_UpdateHandlerMsgBoxShown)
					return;
				m_UpdateHandlerMsgBoxShown = TRUE;
				MessageBox(NULL, _Settings.Translate("Error connecting to remote server."), _Settings.Translate("Wippien update"), MB_ICONERROR);
				m_UpdateHandlerMsgBoxShown = FALSE;
			}
	}
#ifndef _APPUPDLIB
	void _stdcall StateChange(UpdateStates OldState)
	{

	}

	void _stdcall UpdateDone(long ErrorCode, BSTR ErrorText)
	{
	}

	void _stdcall FileStart(IUpdFile* File)
	{

	}

	void _stdcall FileProgress(IUpdFile* File, long Position, long Total)
	{

	}

	void _stdcall FileDone(IUpdFile* File, long ErrorCode, BSTR ErrorText)
	{

	}
#endif


#ifndef _APPUPDLIB
	void _stdcall DownloadDone(long ErrorCode, BSTR ErrorText)
	{
#else
	void AppUpd_DownloadDone(void *wodAppUpd, long ErrorCode, char *ErrorText)
	{
		VARIANT tag;
		WODAPPUPDCOMLib::AppUpd_GetTag(wodAppUpd, &tag);
		CUpdateHandler *m_Owner = (CUpdateHandler *)tag.plVal;
#endif
		if (!ErrorCode)
		{
			if (m_UpdateHandlerMsgBoxShown)
				return;
			m_UpdateHandlerMsgBoxShown = TRUE;
			int i = 0;
			if (m_Owner->m_SilentCheck)
				i = 6;
			else
				i = ::MessageBox(NULL, _Settings.Translate("Download successful. Replace now?"), _Settings.Translate("Wippien update"), MB_ICONQUESTION | MB_YESNO);
			m_UpdateHandlerMsgBoxShown = FALSE;
			if (i == 6)
			{
				m_Owner->m_Silently = TRUE;
				// let's loop through all the contacts and disconnect them
				for (i=0;i<(signed)_MainDlg.m_UserList.m_Users.size();i++)
				{
					CUser *u = (CUser *)_MainDlg.m_UserList.m_Users[i];
					if (u->m_RemoteWippienState != /*WippienState.*/WipWaitingInitRequest)
						u->NotifyDisconnect();
				}
				Sleep(500); // give them time to send out messages

#ifndef _APPUPDLIB
				m_Owner->m_Update->Update();
#else
				WODAPPUPDCOMLib::AppUpd_Update(wodAppUpd);
#endif
			}
		}
		else
		{
			if (!m_Owner->m_SilentCheck)
			{
				CComBSTR2 err = ErrorText;
				MessageBox(NULL, err.ToString(), _Settings.Translate("Wippien update error!"), MB_ICONERROR);
			}

		}
	}

#ifndef _APPUPDLIB
	void _stdcall PrevDetected()
	{
#else
	void AppUpd_PrevDetected(void *wodAppUpd)		
	{
		VARIANT tag;
		WODAPPUPDCOMLib::AppUpd_GetTag(wodAppUpd, &tag);
		CUpdateHandler *m_Owner = (CUpdateHandler *)tag.plVal;
#endif	
		if (m_UpdateHandlerMsgBoxShown)
			return;

		m_UpdateHandlerMsgBoxShown = TRUE;
		int i = 6;
		if (!m_Owner->m_SilentCheck)
			i = ::MessageBox(NULL, _Settings.Translate("New version of Wippien found localy. Replace?"), _Settings.Translate("New version"), MB_ICONQUESTION | MB_YESNO);
		m_UpdateHandlerMsgBoxShown = FALSE;
		if (i==6)
		{
#ifndef _APPUPDLIB
			m_Owner->m_Update->Update();
#else
			WODAPPUPDCOMLib::AppUpd_Update(wodAppUpd);
#endif
			if (::IsWindow(_MainDlg.m_hWnd))
			{
				::PostMessage(_MainDlg.m_hWnd, WM_COMMAND, ID_EXIT, 0);
			}
		}
		else
#ifndef _APPUPDLIB
			m_Owner->m_Update->Reset();
#else
			WODAPPUPDCOMLib::AppUpd_Reset(wodAppUpd);
#endif
	}    

#ifndef _APPUPDLIB    
    BEGIN_SINK_MAP (wodAppUpdateEvents)
        SINK_ENTRY_INFO (1, DIID__IwodAppUpdateComEvents, 0, CloseApp, &INFO_CloseApp)
		SINK_ENTRY_INFO (1, DIID__IwodAppUpdateComEvents, 1, CheckDone, &INFO_CheckDone)
		SINK_ENTRY_INFO (1, DIID__IwodAppUpdateComEvents, 2, StateChange, &INFO_StateChange)
		SINK_ENTRY_INFO (1, DIID__IwodAppUpdateComEvents, 3, UpdateDone, &INFO_UpdateDone)
		SINK_ENTRY_INFO (1, DIID__IwodAppUpdateComEvents, 4, FileStart, &INFO_FileStart)
		SINK_ENTRY_INFO (1, DIID__IwodAppUpdateComEvents, 5, FileProgress, &INFO_FileProgress)
		SINK_ENTRY_INFO (1, DIID__IwodAppUpdateComEvents, 6, FileDone, &INFO_FileDone)
		SINK_ENTRY_INFO (1, DIID__IwodAppUpdateComEvents, 7, DownloadDone, &INFO_DownloadDone)
		SINK_ENTRY_INFO (1, DIID__IwodAppUpdateComEvents, 8, PrevDetected, &INFO_PrevDetected)
    END_SINK_MAP ()

private:
	CUpdateHandler *m_Owner;
};
#endif


CUpdateHandler::CUpdateHandler()
{
	m_Silently = _Settings.m_CheckUpdateSilently;
	m_SilentCheck = _Settings.m_CheckUpdateSilently;
	m_Dlg = NULL;

	m_Update = NULL;
#ifndef _APPUPDLIB
	m_UpdateEvents = NULL;
#endif
	m_NewLanguageFiles = 0;
}

CUpdateHandler::~CUpdateHandler()
{
#ifndef _APPUPDLIB
	if (m_UpdateEvents)
		delete m_UpdateEvents;

	if (m_Update)
		m_Update->Release();
#else
	WODAPPUPDCOMLib::_AppUpd_Destroy(m_Update);
#endif
	m_Update = NULL;

	if (m_Dlg)
		delete m_Dlg;
}

void BufferUncompress(z_stream z_str, char *input_buffer, int len, _Buffer * output_buffer)
{
    unsigned char buf[4096];
    int status;

    z_str.next_in = (unsigned char *)input_buffer;
    z_str.avail_in = len;

    for (;;) {
            /* Set up fixed-size output buffer. */
            z_str.next_out = buf;
            z_str.avail_out = sizeof(buf);

            status = inflate(&z_str, Z_PARTIAL_FLUSH);
            switch (status) {
            case Z_OK:
                    output_buffer->Append((char *)buf,sizeof(buf) - z_str.avail_out);
                    break;
            case Z_BUF_ERROR:
                    /*
                     * Comments in zlib.h say that we should keep calling
                     * inflate() until we get an error.  This appears to 
                     * be the error that we get.
                     */
                    return;
            default:
					return;
                    /* NOTREACHED */
            }
    }
}



void CUpdateHandler::InitUpdater(void)
{
	if (!m_Update)
	{
#ifndef _APPUPDLIB
		HRESULT hr = ::CoCreateInstance(CLSID_wodAppUpdateCom, NULL, CLSCTX_ALL, IID_IwodAppUpdateCom, (void **)&m_Update);
//		HRESULT hr = m_Update.CoCreateInstance(CLSID_wodAppUpdateCom);
		
		if (SUCCEEDED(hr))
		{
			CComBSTR dialognoteurl = "http://www.weonlydo.com/index.asp?showform=AppUpdate&from=Wippien";
			CComBSTR updtername="WippienUpdater";

			if (m_UpdateEvents)
				delete m_UpdateEvents;
			m_UpdateEvents = new wodAppUpdateEvents(this);
			m_Update->put_UpdaterName(updtername);
			m_Update->put_DialogNoteURL(dialognoteurl);
			m_Update->put_ReplaceRule(ReplaceIfVersion);

#ifdef WODAPPUPDATE_LICENSE_KEY
			CComBSTR lickey = WODAPPUPDATE_LICENSE_KEY;
			m_Update->put_LicenseKey(lickey);
#endif		
		}
		else
			return;
#else
		memset (&m_AppUpdEvents, 0, sizeof(m_AppUpdEvents));
		m_AppUpdEvents.CheckDone = AppUpd_CheckDone;
		m_AppUpdEvents.CloseApp = AppUpd_CloseApp;
		m_AppUpdEvents.DownloadDone = AppUpd_DownloadDone;
		m_AppUpdEvents.PrevDetected = AppUpd_PrevDetected;

		m_Update = WODAPPUPDCOMLib::_AppUpd_Create(&m_AppUpdEvents);
		VARIANT var;
		var.vt = VT_I4;
		var.lVal = (LONG)this;

		const char *dialognoteurl = "http://www.weonlydo.com/index.asp?showform=AppUpdate&from=Wippien";
		const char *updtername="WippienUpdater";

		WODAPPUPDCOMLib::AppUpd_SetTag(m_Update, var);
		WODAPPUPDCOMLib::AppUpd_SetUpdaterName(m_Update, (char *)updtername);
		WODAPPUPDCOMLib::AppUpd_SetDialogNoteURL(m_Update, (char *)dialognoteurl);
		WODAPPUPDCOMLib::AppUpd_SetReplaceRule(m_Update, (WODAPPUPDCOMLib::ReplaceRulesEnum)/*ReplaceIfVersion*/1);
		WODAPPUPDCOMLib::AppUpd_SetReplaceRule(m_Update, (WODAPPUPDCOMLib::ReplaceRulesEnum)/*ReplaceIfVersion*/1);
		WODAPPUPDCOMLib::AppUpd_SetRequireAdmin(m_Update, (BOOL)TRUE);
#endif
	}
}

void CUpdateHandler::DownloadUpdates(BOOL silently)
{
	if (m_UpdateHandlerMsgBoxShown)
		return;

	InitUpdater();

	// let's see if we have newer versions
	Buffer URLbuff;
	CComBSTR2 uurl = _Settings.m_UpdateURL;
	URLbuff.Append(_Settings.m_UpdateURL);
	URLbuff.Append("?JID=");
	URLbuff.Append(_Settings.m_JID);


#ifndef _APPUPDLIB
	CComBSTR url = URLbuff.Ptr();
	CComVariant a = url;
	m_Update->put_Visible(silently?VARIANT_FALSE:VARIANT_TRUE);
	m_SilentCheck = silently;
	m_Update->put_ReplaceRule((ReplaceRulesEnum)1); //replaceifnewer
	HRESULT hr = m_Update->Check(a);
	if (SUCCEEDED(hr))
	{
	
	}
#else
	WODAPPUPDCOMLib::AppUpd_SetVisible(m_Update, silently?VARIANT_FALSE:VARIANT_TRUE);
	m_SilentCheck = silently;
	WODAPPUPDCOMLib::AppUpd_SetURL(m_Update, URLbuff.Ptr());
	WODAPPUPDCOMLib::AppUpd_SetReplaceRule(m_Update, (WODAPPUPDCOMLib::ReplaceRulesEnum)1);
	WODAPPUPDCOMLib::AppUpd_SetDialogText(m_Update, 0, _Settings.Translate("Progress"));
	WODAPPUPDCOMLib::AppUpd_SetDialogText(m_Update, 1, _Settings.Translate("Cancel"));
	WODAPPUPDCOMLib::AppUpd_SetDialogText(m_Update, 2, _Settings.Translate("Connecting"));
	WODAPPUPDCOMLib::AppUpd_SetDialogText(m_Update, 3, _Settings.Translate("Requesting"));
	WODAPPUPDCOMLib::AppUpd_SetDialogText(m_Update, 4, _Settings.Translate("Downloading"));
	WODAPPUPDCOMLib::AppUpd_Check(m_Update);
#endif
	// spawn new thread so this can continue
//	DWORD id = 0;
//	CreateThread(NULL, 0, UpdateHandlerThreadProc, this, 0, &id);
	
//	return;
}
