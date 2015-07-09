#include "cshell.h"
#include "settings/csettings.h"
#include "settings.h"
#include "compiler/compiler_warnings_control.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QFileInfo>
#include <QProcessEnvironment>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <thread>
#include <cstdlib>

#ifdef _WIN32
#include <Windows.h>
#endif

QString CShell::shellExecutable()
{
#ifdef _WIN32
	static const QString defaultShell = QProcessEnvironment::systemEnvironment().value("ComSpec", "cmd.exe");
	return CSettings().value(KEY_OTHER_SHELL_COMMAND_NAME, defaultShell).toString();
#elif defined __APPLE__
	return CSettings().value(KEY_OTHER_SHELL_COMMAND_NAME, "/Applications/Utilities/Terminal.app/Contents/MacOS/Terminal").toString();
#elif defined __linux__
	const QString consoleExecutable = CSettings().value(KEY_OTHER_SHELL_COMMAND_NAME).toString();
	if (QFileInfo(consoleExecutable).exists())
		return consoleExecutable;

	static const std::vector<QString> knownTerminals = {
		"/usr/bin/konsole", // KDE
		"/usr/bin/gnome-terminal", // Gnome
		"/usr/bin/pantheon-terminal" // Pantheon (Elementary OS)
	};

	for (const auto& candidate: knownTerminals)
		if (QFileInfo(candidate).exists())
			return candidate;

	return QString();
#else
	#error unknown platform
#endif
}

void CShell::executeShellCommand(const QString& command, const QString& workingDir)
{
	std::thread([command, workingDir](){
	#ifdef _WIN32
		_wsystem((QString("pushd ") + workingDir + " && " + command).toStdWString().data());
	#else
		std::system((QString("cd ") + workingDir + " && " + command).toUtf8().data());
	#endif
	}).detach();
}

#ifdef _WIN32

#include <Shobjidl.h>
#include <ShlObj.h>
#include <shellapi.h>
#include <windowsx.h>

class CItemIdListReleaser {
public:
	explicit CItemIdListReleaser(ITEMIDLIST * idList) : _idList(idList) {}
	~CItemIdListReleaser() { if (_idList) CoTaskMemFree(_idList); }
private:
	ITEMIDLIST * _idList;
};

class CComInterfaceReleaser {
public:
	explicit CComInterfaceReleaser(IUnknown * i) : _i(i) {}
	~CComInterfaceReleaser() { if (_i) _i->Release(); }
private:
	IUnknown * _i;
};

class CItemIdArrayReleaser {
public:
	explicit CItemIdArrayReleaser(std::vector<ITEMIDLIST*> idArray) : _array(idArray) {}
	~CItemIdArrayReleaser() { for (auto it = _array.begin(); it != _array.end(); ++it) if (*it) CoTaskMemFree(*it); }
private:
	std::vector<ITEMIDLIST*> _array;
};

struct ComInitializer {
	ComInitializer() {
		const auto result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		assert_r(SUCCEEDED(result));
	}

	~ComInitializer() {
		CoUninitialize();
	}
};

bool prepareContextMenuForObjects(std::vector<std::wstring> objects, void* parentWindow, HMENU& hmenu, IContextMenu*& imenu);

// Pos must be global

bool CShell::openShellContextMenuForObjects(std::vector<std::wstring> objects, int xPos, int yPos, void * parentWindow)
{
	ComInitializer comInitializer;

	IContextMenu * imenu = 0;
	HMENU hMenu = NULL;
	if (!prepareContextMenuForObjects(objects, parentWindow, hMenu, imenu) || !hMenu || !imenu)
		return false;

	CComInterfaceReleaser menuReleaser(imenu);

	const int iCmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD, xPos, yPos, (HWND)parentWindow, NULL);
	if (iCmd > 0)
	{
		CMINVOKECOMMANDINFOEX info = { 0 };
		info.cbSize = sizeof(info);
		info.fMask = CMIC_MASK_UNICODE;
		info.hwnd = (HWND)parentWindow;
		info.lpVerb  = MAKEINTRESOURCEA(iCmd - 1);
		info.lpVerbW = MAKEINTRESOURCEW(iCmd - 1);
		info.nShow = SW_SHOWNORMAL;
		imenu->InvokeCommand((LPCMINVOKECOMMANDINFO)&info);
	}

	DestroyMenu(hMenu);

	return true;
}

bool CShell::copyObjectsToClipboard(std::vector<std::wstring> objects, void * parentWindow)
{
	ComInitializer comInitializer;

	IContextMenu * imenu = 0;
	HMENU hMenu = NULL;
	if (!prepareContextMenuForObjects(objects, parentWindow, hMenu, imenu) || !hMenu || !imenu)
		return false;

	CComInterfaceReleaser menuReleaser(imenu);

	const int iCmd = 26;
	CMINVOKECOMMANDINFOEX info = { 0 };
	info.cbSize = sizeof(info);
	info.fMask = CMIC_MASK_UNICODE;
	info.hwnd = (HWND)parentWindow;
	info.lpVerb  = MAKEINTRESOURCEA(iCmd - 1);
	info.lpVerbW = MAKEINTRESOURCEW(iCmd - 1);
	info.nShow = SW_SHOWNORMAL;
	const auto result = imenu->InvokeCommand((LPCMINVOKECOMMANDINFO)&info);

	DestroyMenu(hMenu);

	return SUCCEEDED(result);
}

bool CShell::cutObjectsToClipboard(std::vector<std::wstring> objects, void * parentWindow)
{
	ComInitializer comInitializer;

	IContextMenu * imenu = 0;
	HMENU hMenu = NULL;
	if (!prepareContextMenuForObjects(objects, parentWindow, hMenu, imenu) || !hMenu || !imenu)
		return false;

	CComInterfaceReleaser menuReleaser(imenu);

	const int iCmd = 25;
	CMINVOKECOMMANDINFOEX info = { 0 };
	info.cbSize = sizeof(info);
	info.fMask = CMIC_MASK_UNICODE;
	info.hwnd = (HWND)parentWindow;
	info.lpVerb  = MAKEINTRESOURCEA(iCmd - 1);
	info.lpVerbW = MAKEINTRESOURCEW(iCmd - 1);
	info.nShow = SW_SHOWNORMAL;
	const auto result = imenu->InvokeCommand((LPCMINVOKECOMMANDINFO)&info);

	DestroyMenu(hMenu);

	return SUCCEEDED(result);
}

bool CShell::pasteFromClipboard(std::wstring destFolder, void * parentWindow)
{
	ComInitializer comInitializer;

	IContextMenu * imenu = 0;
	HMENU hMenu = NULL;
	if (!prepareContextMenuForObjects(std::vector<std::wstring>(1, destFolder), parentWindow, hMenu, imenu) || !hMenu || !imenu)
		return false;

	CComInterfaceReleaser menuReleaser(imenu);

	const int iCmd = 27;
	CMINVOKECOMMANDINFOEX info = { 0 };
	info.cbSize = sizeof(info);
	info.fMask = CMIC_MASK_UNICODE;
	info.hwnd = (HWND)parentWindow;
	info.lpVerb  = MAKEINTRESOURCEA(iCmd - 1);
	info.lpVerbW = MAKEINTRESOURCEW(iCmd - 1);
	info.nShow = SW_SHOWNORMAL;
	const auto result = imenu->InvokeCommand((LPCMINVOKECOMMANDINFO)&info);

	DestroyMenu(hMenu);

	return SUCCEEDED(result);
}

std::wstring CShell::toolTip(std::wstring itemPath)
{
	ComInitializer comInitializer;

	std::replace(itemPath.begin(), itemPath.end(), '/', '\\');
	std::wstring tipString;
	ITEMIDLIST * id = 0;
	HRESULT result = SHParseDisplayName(itemPath.c_str(), 0, &id, 0, 0);
	if (!SUCCEEDED(result) || !id)
		return tipString;
	CItemIdListReleaser idReleaser (id);

	LPCITEMIDLIST child = 0;
	IShellFolder * ifolder = 0;
	result = SHBindToParent(id, IID_IShellFolder, (void**)&ifolder, &child);
	if (!SUCCEEDED(result) || !child)
		return tipString;

	IQueryInfo* iQueryInfo = 0;
	if (SUCCEEDED(ifolder->GetUIObjectOf(NULL, 1, &child, IID_IQueryInfo, 0, (void**)&iQueryInfo)) && iQueryInfo)
	{
		LPWSTR lpszTip = 0;
		CComInterfaceReleaser releaser (iQueryInfo);
		if (SUCCEEDED(iQueryInfo->GetInfoTip(0, &lpszTip)) && lpszTip)
		{
			tipString = lpszTip;
			CoTaskMemFree(lpszTip);
		}
	}

	std::replace(tipString.begin(), tipString.end(), '\r', '\n');
	return tipString;
}

bool CShell::deleteItems(std::vector<std::wstring> items, bool moveToTrash, void * parentWindow)
{
	ComInitializer comInitializer;

	assert_r(parentWindow);
	std::vector<ITEMIDLIST*> idLists;
	for (auto& path: items)
	{
		__unaligned ITEMIDLIST* idl = ILCreateFromPathW(path.c_str());
		if (!idl)
		{
			for (auto& pid : idLists)
				ILFree(pid);

			qDebug() << "ILCreateFromPathW" << "failed for path" << QString::fromWCharArray(path.c_str());
			return false;
		}
		idLists.push_back(idl);
		assert_r(idLists.back());
	}

	IShellItemArray * iArray = 0;
	HRESULT result = SHCreateShellItemArrayFromIDLists((UINT)idLists.size(), (LPCITEMIDLIST*)idLists.data(), &iArray);

	// Freeing memory
	for (auto& pid: idLists)
		ILFree(pid);
	idLists.clear();

	if (!iArray || !SUCCEEDED(result))
	{
		qDebug() << "SHCreateShellItemArrayFromIDLists failed";
		return false;
	}

	IFileOperation * iOperation = 0;
	result = CoCreateInstance(CLSID_FileOperation, 0, CLSCTX_ALL, IID_IFileOperation, (void**)&iOperation);
	if (!SUCCEEDED(result) || !iOperation)
	{
		qDebug() << "CoCreateInstance(CLSID_FileOperation, 0, CLSCTX_ALL, IID_IFileOperation, (void**)&iOperation) failed";
		return false;
	}

	result = iOperation->DeleteItems(iArray);
	if (!SUCCEEDED(result))
	{
		qDebug() << "DeleteItems failed";
	}
	else
	{
		if (moveToTrash)
		{
			result = iOperation->SetOperationFlags(FOF_ALLOWUNDO);
		}
		else
			result = iOperation->SetOperationFlags(FOF_WANTNUKEWARNING);

		if (!SUCCEEDED(result))
			qDebug() << "SetOperationFlags failed";

		result = iOperation->SetOwnerWindow((HWND) parentWindow);
		if (!SUCCEEDED(result))
			qDebug() << "SetOwnerWindow failed";

		result = iOperation->PerformOperations();
		if (!SUCCEEDED(result) && result != 0x80270000)
			qDebug() << "PerformOperations failed";
		else
			result = S_OK;
	}

	iOperation->Release();
	iArray->Release();
	return SUCCEEDED(result);
}

bool CShell::recycleBinContextMenu(int xPos, int yPos, void *parentWindow)
{
	ComInitializer comInitializer;

	PIDLIST_ABSOLUTE idlist = 0;
	if (!SUCCEEDED(SHGetFolderLocation(0, CSIDL_BITBUCKET, 0, 0, &idlist)))
		return false;

	IShellFolder * iFolder = 0;
	LPCITEMIDLIST list;
	HRESULT result = SHBindToParent(idlist, IID_IShellFolder, (void**)&iFolder, &list);
	if (!SUCCEEDED(result) || !list || !iFolder)
	{
		if (iFolder)
			iFolder->Release();
		return false;
	}

	IContextMenu * imenu = 0;
	result = iFolder->GetUIObjectOf((HWND)parentWindow, 1u, &list, IID_IContextMenu, 0, (void**)&imenu);
	CoTaskMemFree(idlist);
	if (!SUCCEEDED(result) || !imenu)
		return false;
	CComInterfaceReleaser menuReleaser(imenu);

	HMENU hMenu = CreatePopupMenu();
	if (!hMenu)
		return false;
	if (SUCCEEDED(imenu->QueryContextMenu(hMenu, 0, 1, 0x7FFF, CMF_NORMAL)))
	{
		int iCmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD, xPos, yPos, (HWND)parentWindow, NULL);
		if (iCmd > 0)
		{
			CMINVOKECOMMANDINFOEX info = { 0 };
			info.cbSize = sizeof(info);
			info.fMask = CMIC_MASK_UNICODE;
			info.hwnd = (HWND)parentWindow;
			info.lpVerb  = MAKEINTRESOURCEA(iCmd - 1);
			info.lpVerbW = MAKEINTRESOURCEW(iCmd - 1);
			info.nShow = SW_SHOWNORMAL;
			imenu->InvokeCommand((LPCMINVOKECOMMANDINFO)&info);
		}
	}
	DestroyMenu(hMenu);
	return true;
}

bool prepareContextMenuForObjects(std::vector<std::wstring> objects, void * parentWindow, HMENU& hmenu, IContextMenu*& imenu)
{
	ComInitializer comInitializer;

	if (objects.empty())
		return false;

	std::vector<ITEMIDLIST*> ids;
	std::vector<LPCITEMIDLIST> relativeIds;
	IShellFolder * ifolder = 0;
	for (size_t i = 0; i < objects.size(); ++i)
	{
		std::replace(objects[i].begin(), objects[i].end(), '/', '\\');
		ids.push_back(0);
		HRESULT result = SHParseDisplayName(objects[i].c_str(), 0, &ids.back(), 0, 0);
		if (!SUCCEEDED(result) || !ids.back())
		{
			ids.pop_back();
			continue;
		}

		relativeIds.push_back(0);
		result = SHBindToParent(ids.back(), IID_IShellFolder, (void**)&ifolder, &relativeIds.back());
		if (!SUCCEEDED(result) || !relativeIds.back())
			relativeIds.pop_back();
		if (i < objects.size() - 1 && ifolder)
			ifolder->Release();
	}

	CItemIdArrayReleaser arrayReleaser(ids);

	assert_r(parentWindow);
	if (!ifolder)
	{
		qDebug() << "Error getting ifolder";
		return false;
	}
	else if (relativeIds.empty())
	{
		qDebug() << "relativeIds is empty";
		return false;
	}

	imenu = 0;
	HRESULT result = ifolder->GetUIObjectOf((HWND)parentWindow, (UINT)relativeIds.size(), (const ITEMIDLIST **)relativeIds.data(), IID_IContextMenu, 0, (void**)&imenu);
	if (!SUCCEEDED(result) || !imenu)
		return false;

	hmenu = CreatePopupMenu();
	if (!hmenu)
		return false;
	return (SUCCEEDED(imenu->QueryContextMenu(hmenu, 0, 1, 0x7FFF, CMF_NORMAL)));
}

#elif defined __linux__

bool CShell::openShellContextMenuForObjects(std::vector<std::wstring> /*objects*/, int /*xPos*/, int /*yPos*/, void * /*parentWindow*/)
{
	return false;
}

std::wstring CShell::toolTip(std::wstring /*itemPath*/)
{
	return std::wstring();
}

bool CShell::recycleBinContextMenu(int /*xPos*/, int /*yPos*/, void * /*parentWindow*/)
{
	return true;
}

#elif defined __APPLE__

bool CShell::openShellContextMenuForObjects(std::vector<std::wstring> /*objects*/, int /*xPos*/, int /*yPos*/, void */*parentWindow*/)
{
	return false;
}

std::wstring CShell::toolTip(std::wstring /*itemPath*/)
{
	return std::wstring();
}

bool CShell::recycleBinContextMenu(int /*xPos*/, int /*yPos*/, void */*parentWindow*/)
{
	return true;
}

#else
#error unsupported platform
#endif
