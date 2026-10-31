// Open-Shell (c) 2017-2026, The Open-Shell Team

// TilePanel - an optional Windows 10-style tile panel attached to the side of the Windows 7-style main menu.
// The panel is a separate popup window owned by the main menu. It never activates, so the main menu stays
// the foreground window while the user interacts with the tiles.
// The tiles are .lnk files in the TilesPath folder. Their order is stored in the TileOrder registry value

#include "stdafx.h"
#include "TilePanel.h"
#include "MenuContainer.h"
#include "MetroLinkManager.h"
#include "SkinManager.h"
#include "Settings.h"
#include "Translations.h"
#include "ComHelper.h"
#include <windowsx.h>
#include <algorithm>

CTilePanel g_TilePanel;

static const wchar_t *TILES_REG_PATH=L"Software\\OpenShell\\StartMenu";
static const int DEFAULT_COLUMNS=3;

///////////////////////////////////////////////////////////////////////////////

static void ReadTileOrder( std::vector<CString> &order )
{
	order.clear();
	CRegKey regKey;
	if (regKey.Open(HKEY_CURRENT_USER,TILES_REG_PATH,KEY_READ)!=ERROR_SUCCESS)
		return;
	ULONG size=0;
	if (regKey.QueryMultiStringValue(L"TileOrder",NULL,&size)!=ERROR_SUCCESS || size==0)
		return;
	std::vector<wchar_t> buf(size+2,0);
	if (regKey.QueryMultiStringValue(L"TileOrder",&buf[0],&size)!=ERROR_SUCCESS)
		return;
	for (const wchar_t *str=&buf[0];*str;str+=Strlen(str)+1)
		order.push_back(CString(str));
}

static void WriteTileOrder( const std::vector<CString> &order )
{
	CRegKey regKey;
	if (regKey.Open(HKEY_CURRENT_USER,TILES_REG_PATH,KEY_WRITE)!=ERROR_SUCCESS)
		regKey.Create(HKEY_CURRENT_USER,TILES_REG_PATH);
	std::vector<wchar_t> buf;
	for (std::vector<CString>::const_iterator it=order.begin();it!=order.end();++it)
	{
		if (it->IsEmpty()) continue;
		buf.insert(buf.end(),(const wchar_t*)*it,(const wchar_t*)*it+it->GetLength());
		buf.push_back(0);
	}
	buf.push_back(0);
	if (buf.size()==1)
		buf.push_back(0);
	regKey.SetMultiStringValue(L"TileOrder",&buf[0]);
}

// Replaces the characters that are not allowed in file names
static CString MakeValidFileName( const wchar_t *name )
{
	CString res(name?name:L"");
	static const wchar_t *invalid=L"\\/:*?\"<>|";
	for (int i=0;i<res.GetLength();i++)
	{
		wchar_t c=res[i];
		if (c<32 || wcschr(invalid,c))
			res.SetAt(i,'_');
	}
	res.Trim();
	res.TrimRight(L'.');
	if (res.IsEmpty())
		res=L"Tile";
	return res;
}

static COLORREF BlendColor( COLORREF c1, COLORREF c2, int alpha )
{
	int r=(GetRValue(c1)*(255-alpha)+GetRValue(c2)*alpha)/255;
	int g=(GetGValue(c1)*(255-alpha)+GetGValue(c2)*alpha)/255;
	int b=(GetBValue(c1)*(255-alpha)+GetBValue(c2)*alpha)/255;
	return RGB(r,g,b);
}

static bool IsLightColor( COLORREF color )
{
	return (GetRValue(color)*299+GetGValue(color)*587+GetBValue(color)*114)/1000>160;
}

///////////////////////////////////////////////////////////////////////////////

CTilePanel::CTilePanel( void )
{
	m_Owner=NULL;
	m_Width=0;
	m_TileSize=100;
	m_Gap=4;
	m_Margin=12;
	m_Grip=6;
	m_bRightSide=true;
	m_bRTL=false;
	memset(&m_MenuRect,0,sizeof(m_MenuRect));
	memset(&m_WorkArea,0,sizeof(m_WorkArea));
	m_ScrollPos=0;
	m_HotTile=-1;
	m_bTrackMouse=false;
	m_Font=NULL;
	m_DragMode=DRAG_NONE;
	m_DragTile=-1;
	m_DragStart.x=m_DragStart.y=0;
	m_DragStartWidth=0;
	m_InsertIndex=-1;
	m_bInsertAtRowEnd=false;
	m_bDropValid=false;
}

HRESULT STDMETHODCALLTYPE CTilePanel::QueryInterface( REFIID riid, void **ppvObject )
{
	if (!ppvObject) return E_POINTER;
	*ppvObject=NULL;
	if (riid==IID_IUnknown || riid==IID_IDropTarget)
	{
		*ppvObject=static_cast<IDropTarget*>(this);
		return S_OK;
	}
	return E_NOINTERFACE;
}

void CTilePanel::CreatePanel( HWND menu )
{
	if (m_hWnd)
		DestroyWindow();
	m_hWnd=NULL;

	m_Owner=menu;
	m_bRTL=(::GetWindowLong(menu,GWL_EXSTYLE)&WS_EX_LAYOUTRTL)!=0;
	int dpi=CItemManager::GetDPI(true);
	int tileSize=GetSettingInt(L"TileSize");
	if (tileSize<32) tileSize=32;
	if (tileSize>400) tileSize=400;
	m_TileSize=tileSize*dpi/96;
	m_Gap=4*dpi/96;
	m_Margin=12*dpi/96;
	m_Grip=6*dpi/96;
	if (m_Grip<4) m_Grip=4;

	m_Width=0;
	CRegKey regKey;
	DWORD width;
	if (regKey.Open(HKEY_CURRENT_USER,TILES_REG_PATH,KEY_READ)==ERROR_SUCCESS && regKey.QueryDWORDValue(L"TilePanelWidth",width)==ERROR_SUCCESS && width>0 && width<10000)
		m_Width=width*dpi/96;
	if (m_Width<=0)
		m_Width=m_Grip+2*m_Margin+DEFAULT_COLUMNS*m_TileSize+(DEFAULT_COLUMNS-1)*m_Gap;

	wchar_t path[_MAX_PATH];
	Strcpy(path,_countof(path),GetSettingString(L"TilesPath"));
	DoEnvironmentSubst(path,_countof(path));
	PathRemoveBackslash(path);
	m_Folder=path;

	m_ScrollPos=0;
	m_HotTile=-1;
	m_DragMode=DRAG_NONE;
	m_InsertIndex=-1;
	memset(&m_MenuRect,0,sizeof(m_MenuRect));

	NONCLIENTMETRICS metrics={sizeof(metrics)};
	SystemParametersInfo(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0);
	if (m_Font) DeleteObject(m_Font);
	m_Font=CreateFontIndirect(&metrics.lfMessageFont);

	LoadTiles();

	bool bTopmost=(::GetWindowLong(menu,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
	Create(menu,NULL,NULL,WS_POPUP|WS_CLIPCHILDREN,WS_EX_TOOLWINDOW|(bTopmost?WS_EX_TOPMOST:0));
}

LRESULT CTilePanel::OnCreate( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	RegisterDragDrop(m_hWnd,this);
	return 0;
}

LRESULT CTilePanel::OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	RevokeDragDrop(m_hWnd);
	m_pDropTargetHelper.Release();
	if (m_Font) DeleteObject(m_Font);
	m_Font=NULL;
	m_Tiles.clear();
	m_Owner=NULL;
	m_DragMode=DRAG_NONE;
	bHandled=FALSE;
	return 0;
}

///////////////////////////////////////////////////////////////////////////////
// tiles storage

bool CTilePanel::EnsureFolder( void )
{
	if (m_Folder.IsEmpty()) return false;
	DWORD attr=GetFileAttributes(m_Folder);
	if (attr!=INVALID_FILE_ATTRIBUTES && (attr&FILE_ATTRIBUTE_DIRECTORY))
		return true;
	int res=SHCreateDirectory(NULL,m_Folder);
	return res==ERROR_SUCCESS || res==ERROR_ALREADY_EXISTS || res==ERROR_FILE_EXISTS;
}

void CTilePanel::LoadTiles( void )
{
	m_Tiles.clear();
	if (m_Folder.IsEmpty()) return;

	std::vector<CString> files;
	WIN32_FIND_DATA data;
	CString pattern=m_Folder+L"\\*.lnk";
	HANDLE h=FindFirstFile(pattern,&data);
	if (h!=INVALID_HANDLE_VALUE)
	{
		do
		{
			if (data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_HIDDEN))
				continue;
			if (_wcsicmp(PathFindExtension(data.cFileName),L".lnk")!=0)
				continue;
			files.push_back(CString(data.cFileName));
		} while (FindNextFile(h,&data));
		FindClose(h);
	}
	std::sort(files.begin(),files.end(),[]( const CString &a, const CString &b ) { return StrCmpLogicalW(a,b)<0; });

	// first add the files in the saved order, then the rest alphabetically
	std::vector<CString> order;
	ReadTileOrder(order);
	std::vector<bool> used(files.size(),false);
	std::vector<CString> sorted;
	for (std::vector<CString>::const_iterator it=order.begin();it!=order.end();++it)
	{
		for (size_t i=0;i<files.size();i++)
			if (!used[i] && _wcsicmp(files[i],*it)==0)
			{
				used[i]=true;
				sorted.push_back(files[i]);
				break;
			}
	}
	for (size_t i=0;i<files.size();i++)
		if (!used[i])
			sorted.push_back(files[i]);

	for (std::vector<CString>::const_iterator it=sorted.begin();it!=sorted.end();++it)
	{
		Tile tile;
		tile.fileName=*it;
		tile.path=m_Folder+L"\\"+*it;
		tile.name=*it;
		PathRemoveExtension(tile.name.GetBuffer());
		tile.name.ReleaseBuffer();
		tile.pInfo=g_ItemManager.GetItemInfo(tile.path,CItemManager::INFO_LINK|CItemManager::INFO_METRO|CItemManager::INFO_EXTRA_LARGE_ICON);
		m_Tiles.push_back(tile);
	}
}

void CTilePanel::SaveOrder( void )
{
	std::vector<CString> order;
	for (std::vector<Tile>::const_iterator it=m_Tiles.begin();it!=m_Tiles.end();++it)
		order.push_back(it->fileName);
	WriteTileOrder(order);
}

bool CTilePanel::CreateTileLink( PCIDLIST_ABSOLUTE pidl, const wchar_t *name, CString &fileName )
{
	if (!pidl || !EnsureFolder()) return false;

	// if the item is a shortcut file, just copy it
	CComPtr<IShellItem> pItem;
	if (SUCCEEDED(SHCreateItemFromIDList(pidl,IID_IShellItem,(void**)&pItem)))
	{
		SFGAOF attr=0;
		CComString pPath;
		if (SUCCEEDED(pItem->GetAttributes(SFGAO_LINK|SFGAO_FILESYSTEM|SFGAO_FOLDER,&attr)) && (attr&(SFGAO_LINK|SFGAO_FILESYSTEM))==(SFGAO_LINK|SFGAO_FILESYSTEM)
			&& SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH,&pPath)) && _wcsicmp(PathFindExtension(pPath),L".lnk")==0)
		{
			wchar_t finalPath[_MAX_PATH];
			if (PathYetAnotherMakeUniqueName(finalPath,m_Folder,NULL,PathFindFileName(pPath)) && CopyFile(pPath,finalPath,TRUE))
			{
				fileName=PathFindFileName(finalPath);
				return true;
			}
		}
	}

	CString finalPath;
	if (FAILED(CreatePinLink(pidl,m_Folder,MakeValidFileName(name),NULL,0,&finalPath)) || finalPath.IsEmpty())
		return false;
	fileName=PathFindFileName(finalPath);
	return true;
}

void CTilePanel::InsertNewTiles( const std::vector<CString> &fileNames, int insertIndex )
{
	if (fileNames.empty()) return;
	std::vector<CString> order;
	for (std::vector<Tile>::const_iterator it=m_Tiles.begin();it!=m_Tiles.end();++it)
		order.push_back(it->fileName);
	if (insertIndex<0 || insertIndex>(int)order.size())
		insertIndex=(int)order.size();
	order.insert(order.begin()+insertIndex,fileNames.begin(),fileNames.end());
	WriteTileOrder(order);
	LoadTiles();
	m_HotTile=-1;
	ClampScroll();
	if (m_hWnd) Invalidate();
}

bool CTilePanel::PinItem( PCIDLIST_ABSOLUTE pidl, const wchar_t *name, int insertIndex )
{
	CString fileName;
	if (!CreateTileLink(pidl,name,fileName))
		return false;
	std::vector<CString> names(1,fileName);
	InsertNewTiles(names,insertIndex);
	return true;
}

///////////////////////////////////////////////////////////////////////////////
// layout

int CTilePanel::GetColumns( void ) const
{
	int width=m_Width;
	if (m_hWnd)
	{
		RECT rc;
		GetClientRect(&rc);
		if (rc.right>0) width=rc.right;
	}
	int cols=(width-m_Grip-2*m_Margin+m_Gap)/(m_TileSize+m_Gap);
	return cols<1?1:cols;
}

int CTilePanel::GetMinWidth( void ) const
{
	return m_Grip+2*m_Margin+m_TileSize;
}

int CTilePanel::GetMaxWidth( void ) const
{
	return m_bRightSide?(m_WorkArea.right-m_MenuRect.right):(m_MenuRect.left-m_WorkArea.left);
}

int CTilePanel::GetContentHeight( void ) const
{
	int cols=GetColumns();
	int rows=((int)m_Tiles.size()+cols-1)/cols;
	if (rows==0) return 0;
	return 2*m_Margin+rows*m_TileSize+(rows-1)*m_Gap;
}

void CTilePanel::ClampScroll( void )
{
	int height=0;
	if (m_hWnd)
	{
		RECT rc;
		GetClientRect(&rc);
		height=rc.bottom;
	}
	int maxScroll=GetContentHeight()-height;
	if (m_ScrollPos>maxScroll) m_ScrollPos=maxScroll;
	if (m_ScrollPos<0) m_ScrollPos=0;
}

void CTilePanel::GetGripRect( RECT &rc ) const
{
	GetClientRect(&rc);
	if (m_bRightSide)
		rc.left=rc.right-m_Grip;
	else
		rc.right=rc.left+m_Grip;
}

void CTilePanel::GetTileRect( int index, RECT &rc ) const
{
	RECT client;
	GetClientRect(&client);
	int cols=GetColumns();
	int row=index/cols, col=index%cols;
	int step=m_TileSize+m_Gap;
	if (m_bRTL)
	{
		// fill from right to left
		int x1=client.right-(m_bRightSide?m_Grip:0)-m_Margin;
		rc.right=x1-col*step;
		rc.left=rc.right-m_TileSize;
	}
	else
	{
		int x0=(m_bRightSide?0:m_Grip)+m_Margin;
		rc.left=x0+col*step;
		rc.right=rc.left+m_TileSize;
	}
	rc.top=m_Margin+row*step-m_ScrollPos;
	rc.bottom=rc.top+m_TileSize;
}

int CTilePanel::HitTestTile( POINT pt ) const
{
	for (int i=0;i<(int)m_Tiles.size();i++)
	{
		RECT rc;
		GetTileRect(i,rc);
		if (PtInRect(&rc,pt))
			return i;
	}
	return -1;
}

int CTilePanel::GetInsertIndex( POINT pt, bool *pbRowEnd ) const
{
	RECT client;
	GetClientRect(&client);
	int cols=GetColumns();
	int step=m_TileSize+m_Gap;
	int x;
	if (m_bRTL)
		x=client.right-(m_bRightSide?m_Grip:0)-m_Margin-pt.x;
	else
		x=pt.x-(m_bRightSide?0:m_Grip)-m_Margin;
	int col=(x+step/2)/step;
	if (x<0) col=0;
	if (col>cols) col=cols;
	int y=pt.y-m_Margin+m_ScrollPos;
	int row=y<0?0:y/step;
	int count=(int)m_Tiles.size();
	int maxRow=count/cols;
	if (row>maxRow) row=maxRow;
	int index=row*cols+col;
	bool bRowEnd=(col==cols);
	if (index>=count)
	{
		index=count;
		bRowEnd=false;
	}
	if (pbRowEnd) *pbRowEnd=bRowEnd;
	return index;
}

///////////////////////////////////////////////////////////////////////////////
// positioning

void CTilePanel::UpdatePosition( const RECT &menuRect, bool bMenuLeft, bool bVisible )
{
	if (!m_hWnd) return;
	if (!bVisible || menuRect.right<=menuRect.left || menuRect.bottom<=menuRect.top)
	{
		HidePanel();
		return;
	}
	m_MenuRect=menuRect;
	m_bRightSide=bMenuLeft; // the menu grows to the right, so the panel goes on its right side
	MONITORINFO info={sizeof(info)};
	GetMonitorInfo(MonitorFromRect(&menuRect,MONITOR_DEFAULTTONEAREST),&info);
	m_WorkArea=info.rcWork;
	ApplyPosition();
}

void CTilePanel::ApplyPosition( void )
{
	if (!m_hWnd) return;
	int maxWidth=GetMaxWidth();
	if (maxWidth<GetMinWidth())
	{
		// no room next to the menu
		HidePanel();
		return;
	}
	int width=m_Width;
	if (width<GetMinWidth()) width=GetMinWidth();
	if (width>maxWidth) width=maxWidth;

	RECT rc;
	rc.top=m_MenuRect.top;
	rc.bottom=m_MenuRect.bottom;
	if (m_bRightSide)
	{
		rc.left=m_MenuRect.right;
		rc.right=rc.left+width;
	}
	else
	{
		rc.right=m_MenuRect.left;
		rc.left=rc.right-width;
	}

	bool bWasVisible=IsWindowVisible()!=FALSE;
	RECT old;
	GetWindowRect(&old);
	if (bWasVisible && EqualRect(&old,&rc))
		return;
	SetWindowPos(NULL,&rc,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_SHOWWINDOW);
	ClampScroll();
	// paint immediately, the menu may be running its animation loop without processing messages
	RedrawWindow(NULL,NULL,RDW_INVALIDATE|RDW_UPDATENOW);
}

void CTilePanel::HidePanel( void )
{
	if (!m_hWnd) return;
	if (m_DragMode!=DRAG_NONE)
		EndDrag(true);
	if (IsWindowVisible())
		SetWindowPos(NULL,0,0,0,0,SWP_NOZORDER|SWP_NOSIZE|SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_HIDEWINDOW);
	SetHotTile(-1);
}

void CTilePanel::RefreshIcons( void )
{
	if (m_hWnd)
		Invalidate();
}

void CTilePanel::SetWidth( int width )
{
	int maxWidth=GetMaxWidth();
	if (width>maxWidth) width=maxWidth;
	if (width<GetMinWidth()) width=GetMinWidth();
	if (width==m_Width) return;
	m_Width=width;
	ApplyPosition();
	ClampScroll();
	Invalidate();
	UpdateWindow();
}

void CTilePanel::SaveWidth( void )
{
	RECT rc;
	GetWindowRect(&rc);
	int width=rc.right-rc.left;
	if (width<=0) return;
	m_Width=width;
	int dpi=CItemManager::GetDPI(true);
	CRegKey regKey;
	if (regKey.Open(HKEY_CURRENT_USER,TILES_REG_PATH,KEY_WRITE)!=ERROR_SUCCESS)
		regKey.Create(HKEY_CURRENT_USER,TILES_REG_PATH);
	regKey.SetDWORDValue(L"TilePanelWidth",(DWORD)((width*96+dpi/2)/dpi));
}

///////////////////////////////////////////////////////////////////////////////
// painting

LRESULT CTilePanel::OnPaint( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	PAINTSTRUCT ps;
	HDC hdc;
	if (uMsg==WM_PRINTCLIENT)
		hdc=(HDC)wParam;
	else
		hdc=BeginPaint(&ps);

	RECT rc;
	GetClientRect(&rc);
	if (rc.right>0 && rc.bottom>0)
	{
		// double buffering
		HDC hdcMem=CreateCompatibleDC(hdc);
		HBITMAP bmp=CreateCompatibleBitmap(hdc,rc.right,rc.bottom);
		HGDIOBJ bmp0=SelectObject(hdcMem,bmp);
		Draw(hdcMem,rc);
		BitBlt(hdc,0,0,rc.right,rc.bottom,hdcMem,0,0,SRCCOPY);
		SelectObject(hdcMem,bmp0);
		DeleteObject(bmp);
		DeleteDC(hdcMem);
	}

	if (uMsg!=WM_PRINTCLIENT)
		EndPaint(&ps);
	return 0;
}

static void FillSolidRect( HDC hdc, const RECT &rc, COLORREF color )
{
	SetDCBrushColor(hdc,color);
	FillRect(hdc,&rc,(HBRUSH)GetStockObject(DC_BRUSH));
}

void CTilePanel::Draw( HDC hdc, const RECT &clientRect )
{
	int r=0, g=0, b=0;
	GetMetroGlassColor(r,g,b);
	COLORREF bkColor=RGB(r,g,b);
	COLORREF tileColor=GetSystemAccentColor();
	COLORREF textColor=IsLightColor(tileColor)?RGB(0,0,0):RGB(255,255,255);
	COLORREF hotColor=BlendColor(tileColor,IsLightColor(tileColor)?RGB(0,0,0):RGB(255,255,255),48);

	FillSolidRect(hdc,clientRect,bkColor);

	// resize grip
	{
		RECT grip;
		GetGripRect(grip);
		COLORREF gripColor=BlendColor(bkColor,IsLightColor(bkColor)?RGB(0,0,0):RGB(255,255,255),m_DragMode==DRAG_RESIZE?80:24);
		FillSolidRect(hdc,grip,gripColor);
	}

	HFONT font0=(HFONT)SelectObject(hdc,m_Font?m_Font:GetStockObject(DEFAULT_GUI_FONT));
	SetBkMode(hdc,TRANSPARENT);
	TEXTMETRIC tm;
	GetTextMetrics(hdc,&tm);
	int textHeight=tm.tmHeight;
	int textPad=6*CItemManager::GetDPI(true)/96;

	if (m_Tiles.empty())
	{
		RECT rc=clientRect;
		if (m_bRightSide) rc.right-=m_Grip; else rc.left+=m_Grip;
		InflateRect(&rc,-m_Margin,-m_Margin);
		SetTextColor(hdc,BlendColor(bkColor,IsLightColor(bkColor)?RGB(0,0,0):RGB(255,255,255),160));
		DrawText(hdc,FindTranslation(L"Menu.TilesEmpty",L"Right-click a program and choose \"Pin to tiles\", or drag it here."),-1,&rc,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX|(m_bRTL?DT_RTLREADING:0));
	}

	// collect the icon bitmaps
	std::vector<HBITMAP> icons(m_Tiles.size(),(HBITMAP)NULL);
	{
		CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
		for (size_t i=0;i<m_Tiles.size();i++)
		{
			const CItemManager::ItemInfo *pInfo=m_Tiles[i].pInfo;
			if (pInfo && pInfo->extraLargeIcon)
				icons[i]=pInfo->extraLargeIcon->bitmap;
		}
	}

	HDC hdcIcon=CreateCompatibleDC(hdc);
	for (int i=0;i<(int)m_Tiles.size();i++)
	{
		RECT rc;
		GetTileRect(i,rc);
		if (rc.bottom<clientRect.top || rc.top>clientRect.bottom)
			continue;
		bool bDragged=(m_DragMode==DRAG_REORDER && i==m_DragTile);
		COLORREF color=(i==m_HotTile && m_DragMode!=DRAG_REORDER)?hotColor:tileColor;
		if (bDragged)
			color=BlendColor(tileColor,bkColor,128);
		FillSolidRect(hdc,rc,color);

		// icon
		if (icons[i])
		{
			BITMAP info;
			if (GetObject(icons[i],sizeof(info),&info))
			{
				int w=info.bmWidth, h=abs(info.bmHeight);
				int maxSize=m_TileSize*3/5;
				int size=w>h?w:h;
				if (size>maxSize && size>0)
				{
					w=w*maxSize/size;
					h=h*maxSize/size;
				}
				int x=(rc.left+rc.right-w)/2;
				int y=(rc.top+rc.bottom-h)/2-textHeight/2;
				if (y<rc.top+textPad) y=rc.top+textPad;
				HGDIOBJ bmp0=SelectObject(hdcIcon,icons[i]);
				BLENDFUNCTION func={AC_SRC_OVER,0,(BYTE)(bDragged?128:255),AC_SRC_ALPHA};
				AlphaBlend(hdc,x,y,w,h,hdcIcon,0,0,info.bmWidth,abs(info.bmHeight),func);
				SelectObject(hdcIcon,bmp0);
			}
		}

		// name
		RECT rcText=rc;
		InflateRect(&rcText,-textPad,-textPad/2);
		SetTextColor(hdc,textColor);
		DrawText(hdc,m_Tiles[i].name,-1,&rcText,DT_SINGLELINE|DT_BOTTOM|DT_END_ELLIPSIS|DT_NOPREFIX|(m_bRTL?(DT_RIGHT|DT_RTLREADING):DT_LEFT));
	}
	DeleteDC(hdcIcon);

	// insert mark
	if (m_InsertIndex>=0 && (m_DragMode==DRAG_REORDER || m_bDropValid))
	{
		int count=(int)m_Tiles.size();
		RECT rc;
		bool bAfter=false;
		if (count==0)
		{
			GetTileRect(0,rc);
		}
		else if (m_InsertIndex>=count || m_bInsertAtRowEnd)
		{
			GetTileRect(m_InsertIndex-1,rc);
			bAfter=true;
		}
		else
			GetTileRect(m_InsertIndex,rc);
		int markWidth=m_Gap>2?m_Gap:2;
		// in RTL mode the "after" side is on the left
		bool bRightEdge=(bAfter!=m_bRTL);
		RECT mark=rc;
		if (bRightEdge)
		{
			mark.left=rc.right+(m_Gap-markWidth)/2;
			mark.right=mark.left+markWidth;
		}
		else
		{
			mark.right=rc.left-(m_Gap-markWidth)/2;
			mark.left=mark.right-markWidth;
		}
		FillSolidRect(hdc,mark,IsLightColor(bkColor)?RGB(0,0,0):RGB(255,255,255));
	}

	SelectObject(hdc,font0);
}

///////////////////////////////////////////////////////////////////////////////
// mouse

void CTilePanel::SetHotTile( int index )
{
	if (index==m_HotTile) return;
	m_HotTile=index;
	if (m_hWnd) Invalidate();
}

LRESULT CTilePanel::OnSetCursor( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (LOWORD(lParam)==HTCLIENT)
	{
		POINT pt;
		GetCursorPos(&pt);
		ScreenToClient(&pt);
		RECT grip;
		GetGripRect(grip);
		if (m_DragMode==DRAG_RESIZE || PtInRect(&grip,pt))
		{
			SetCursor(LoadCursor(NULL,IDC_SIZEWE));
			return TRUE;
		}
	}
	bHandled=FALSE;
	return 0;
}

LRESULT CTilePanel::OnMouseMove( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	POINT pt={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
	if (!m_bTrackMouse)
	{
		TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,m_hWnd,0};
		TrackMouseEvent(&track);
		m_bTrackMouse=true;
	}

	if (m_DragMode==DRAG_RESIZE)
	{
		POINT pos=pt;
		ClientToScreen(&pos);
		int dx=pos.x-m_DragStart.x;
		SetWidth(m_DragStartWidth+(m_bRightSide?dx:-dx));
		return 0;
	}

	if (m_DragMode==DRAG_PENDING)
	{
		POINT pos=pt;
		ClientToScreen(&pos);
		if (abs(pos.x-m_DragStart.x)>=GetSystemMetrics(SM_CXDRAG) || abs(pos.y-m_DragStart.y)>=GetSystemMetrics(SM_CYDRAG))
		{
			m_DragMode=DRAG_REORDER;
			SetHotTile(-1);
		}
	}

	if (m_DragMode==DRAG_REORDER)
	{
		bool bRowEnd;
		int index=GetInsertIndex(pt,&bRowEnd);
		if (index!=m_InsertIndex || bRowEnd!=m_bInsertAtRowEnd)
		{
			m_InsertIndex=index;
			m_bInsertAtRowEnd=bRowEnd;
			Invalidate();
		}
		SetCursor(LoadCursor(NULL,IDC_ARROW));
		return 0;
	}

	SetHotTile(HitTestTile(pt));
	return 0;
}

LRESULT CTilePanel::OnMouseLeave( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	m_bTrackMouse=false;
	if (m_DragMode==DRAG_NONE)
		SetHotTile(-1);
	return 0;
}

LRESULT CTilePanel::OnMouseWheel( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	int delta=GET_WHEEL_DELTA_WPARAM(wParam);
	int old=m_ScrollPos;
	m_ScrollPos-=delta*(m_TileSize+m_Gap)/WHEEL_DELTA;
	ClampScroll();
	if (old!=m_ScrollPos)
	{
		POINT pt;
		GetCursorPos(&pt);
		ScreenToClient(&pt);
		m_HotTile=HitTestTile(pt);
		Invalidate();
	}
	return 0;
}

LRESULT CTilePanel::OnLButtonDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	POINT pt={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
	POINT pos=pt;
	ClientToScreen(&pos);
	RECT grip;
	GetGripRect(grip);
	if (PtInRect(&grip,pt))
	{
		RECT rc;
		GetWindowRect(&rc);
		m_DragMode=DRAG_RESIZE;
		m_DragStart=pos;
		m_DragStartWidth=rc.right-rc.left;
		m_Width=m_DragStartWidth;
		SetCapture();
		Invalidate();
		return 0;
	}
	int index=HitTestTile(pt);
	if (index>=0)
	{
		m_DragMode=DRAG_PENDING;
		m_DragTile=index;
		m_DragStart=pos;
		m_InsertIndex=-1;
		SetCapture();
	}
	return 0;
}

LRESULT CTilePanel::OnLButtonUp( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	POINT pt={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
	TDragMode mode=m_DragMode;
	int dragTile=m_DragTile;
	int insertIndex=m_InsertIndex;
	m_DragMode=DRAG_NONE; // clear before releasing the capture, so OnCaptureChanged doesn't cancel anything
	m_DragTile=-1;
	m_InsertIndex=-1;
	if (GetCapture()==m_hWnd)
		ReleaseCapture();

	if (mode==DRAG_RESIZE)
	{
		SaveWidth();
		Invalidate();
	}
	else if (mode==DRAG_REORDER)
	{
		if (dragTile>=0 && dragTile<(int)m_Tiles.size() && insertIndex>=0)
		{
			if (insertIndex>dragTile) insertIndex--;
			if (insertIndex!=dragTile)
			{
				Tile tile=m_Tiles[dragTile];
				m_Tiles.erase(m_Tiles.begin()+dragTile);
				if (insertIndex>(int)m_Tiles.size()) insertIndex=(int)m_Tiles.size();
				m_Tiles.insert(m_Tiles.begin()+insertIndex,tile);
				SaveOrder();
			}
		}
		SetHotTile(HitTestTile(pt));
		Invalidate();
	}
	else if (mode==DRAG_PENDING)
	{
		if (dragTile>=0 && dragTile==HitTestTile(pt))
			LaunchTile(dragTile);
	}
	return 0;
}

LRESULT CTilePanel::OnCaptureChanged( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if ((HWND)lParam!=m_hWnd && m_DragMode!=DRAG_NONE)
		EndDrag(true);
	return 0;
}

void CTilePanel::EndDrag( bool bCancel )
{
	TDragMode mode=m_DragMode;
	m_DragMode=DRAG_NONE;
	m_DragTile=-1;
	m_InsertIndex=-1;
	if (GetCapture()==m_hWnd)
		ReleaseCapture();
	if (mode==DRAG_RESIZE)
		SaveWidth(); // keep the width the user got to
	if (m_hWnd)
		Invalidate();
}

LRESULT CTilePanel::OnRButtonUp( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled )
{
	if (m_DragMode!=DRAG_NONE) return 0;
	POINT pt={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
	int index=HitTestTile(pt);
	if (index<0) return 0;
	ClientToScreen(&pt);
	ShowTileMenu(index,pt);
	return 0;
}

///////////////////////////////////////////////////////////////////////////////
// commands

void CTilePanel::LaunchTile( int index )
{
	if (index<0 || index>=(int)m_Tiles.size()) return;
	CString path=m_Tiles[index].path;
	const CItemManager::ItemInfo *pInfo=m_Tiles[index].pInfo;
	bool bMetroLink=false;
	if (pInfo)
	{
		CItemManager::RWLock lock(&g_ItemManager,false,CItemManager::RWLOCK_ITEMS);
		bMetroLink=pInfo->IsMetroLink();
	}

	CMenuContainer::PlayMenuSound(SOUND_COMMAND);
	LockSetForegroundWindow(LSFW_UNLOCK);

	// close the menu the same way as when a menu item is executed. this doesn't restore the previous foreground window
	CMenuContainer::s_bPreventClosing=true;
	CMenuContainer::CloseSubMenus(CMenuContainer::CLOSE_POST,NULL);
	CMenuContainer::s_bPreventClosing=false;

	if (bMetroLink)
		ExecuteMetroLink(pInfo);
	else
	{
		SHELLEXECUTEINFO execute={sizeof(execute),SEE_MASK_FLAG_LOG_USAGE};
		execute.lpVerb=L"open";
		execute.lpFile=path;
		execute.nShow=SW_SHOWNORMAL;
		ShellExecuteEx(&execute);
	}
}

void CTilePanel::ShowTileMenu( int index, POINT pt )
{
	enum
	{
		CMD_TILE_OPEN=1,
		CMD_TILE_LOCATION,
		CMD_TILE_UNPIN,
	};

	HMENU menu=CreatePopupMenu();
	AppendMenu(menu,MF_STRING,CMD_TILE_OPEN,FindTranslation(L"Menu.Open",L"&Open"));
	SetMenuDefaultItem(menu,CMD_TILE_OPEN,FALSE);
	AppendMenu(menu,MF_STRING,CMD_TILE_LOCATION,FindTranslation(L"Menu.OpenTileLocation",L"Open file &location"));
	AppendMenu(menu,MF_SEPARATOR,0,NULL);
	AppendMenu(menu,MF_STRING,CMD_TILE_UNPIN,FindTranslation(L"Menu.UnpinTile",L"&Unpin from tiles"));

	SetHotTile(index);
	// the menu is owned by the main menu window because it is the foreground window, so the popup closes properly
	int res=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON|(m_bRTL?TPM_LAYOUTRTL:0),pt.x,pt.y,0,m_Owner?m_Owner:m_hWnd,NULL);
	DestroyMenu(menu);
	if (!m_hWnd || index>=(int)m_Tiles.size())
		return;

	if (res==CMD_TILE_OPEN)
	{
		LaunchTile(index);
	}
	else if (res==CMD_TILE_LOCATION)
	{
		// select the target of the shortcut, or the shortcut itself if the target is not a file
		CString select=m_Tiles[index].path;
		{
			CComPtr<IShellLink> pLink;
			if (SUCCEEDED(pLink.CoCreateInstance(CLSID_ShellLink)))
			{
				CComQIPtr<IPersistFile> pFile(pLink);
				wchar_t target[_MAX_PATH];
				if (pFile && SUCCEEDED(pFile->Load(select,STGM_READ)) && pLink->GetPath(target,_countof(target),NULL,SLGP_RAWPATH)==S_OK && target[0])
				{
					wchar_t expanded[_MAX_PATH];
					if (ExpandEnvironmentStrings(target,expanded,_countof(expanded)) && GetFileAttributes(expanded)!=INVALID_FILE_ATTRIBUTES)
						select=expanded;
				}
			}
		}
		CMenuContainer::PlayMenuSound(SOUND_COMMAND);
		LockSetForegroundWindow(LSFW_UNLOCK);
		CMenuContainer::s_bPreventClosing=true;
		CMenuContainer::CloseSubMenus(CMenuContainer::CLOSE_POST,NULL);
		CMenuContainer::s_bPreventClosing=false;
		CString params;
		params.Format(L"/select,\"%s\"",(const wchar_t*)select);
		ShellExecute(NULL,NULL,L"explorer.exe",params,NULL,SW_SHOWNORMAL);
	}
	else if (res==CMD_TILE_UNPIN)
	{
		DeleteFile(m_Tiles[index].path);
		m_Tiles.erase(m_Tiles.begin()+index);
		SaveOrder();
		m_HotTile=-1;
		ClampScroll();
		Invalidate();
	}
	else
	{
		POINT pos;
		GetCursorPos(&pos);
		ScreenToClient(&pos);
		SetHotTile(HitTestTile(pos));
	}
}

///////////////////////////////////////////////////////////////////////////////
// IDropTarget

static DWORD GetTileDropEffect( DWORD allowed )
{
	if (allowed&DROPEFFECT_LINK) return DROPEFFECT_LINK;
	if (allowed&DROPEFFECT_COPY) return DROPEFFECT_COPY;
	return DROPEFFECT_NONE;
}

HRESULT STDMETHODCALLTYPE CTilePanel::DragEnter( IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect )
{
	m_bDropValid=false;
	if (pDataObj)
	{
		CComPtr<IShellItemArray> pArray;
		if (SUCCEEDED(SHCreateShellItemArrayFromDataObject(pDataObj,IID_IShellItemArray,(void**)&pArray)))
			m_bDropValid=true;
	}
	DWORD effect=m_bDropValid?GetTileDropEffect(*pdwEffect):DROPEFFECT_NONE;
	if (!m_pDropTargetHelper)
		m_pDropTargetHelper.CoCreateInstance(CLSID_DragDropHelper);
	POINT p={pt.x,pt.y};
	if (m_pDropTargetHelper)
		m_pDropTargetHelper->DragEnter(m_hWnd,pDataObj,&p,effect);
	*pdwEffect=effect;
	return DragOver(grfKeyState,pt,pdwEffect);
}

HRESULT STDMETHODCALLTYPE CTilePanel::DragOver( DWORD grfKeyState, POINTL pt, DWORD *pdwEffect )
{
	POINT p={pt.x,pt.y};
	DWORD effect=m_bDropValid?GetTileDropEffect(*pdwEffect):DROPEFFECT_NONE;
	if (m_pDropTargetHelper)
		m_pDropTargetHelper->DragOver(&p,effect);
	if (m_bDropValid)
	{
		POINT client=p;
		ScreenToClient(&client);
		bool bRowEnd;
		int index=GetInsertIndex(client,&bRowEnd);
		if (index!=m_InsertIndex || bRowEnd!=m_bInsertAtRowEnd)
		{
			m_InsertIndex=index;
			m_bInsertAtRowEnd=bRowEnd;
			Invalidate();
		}
	}
	*pdwEffect=effect;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CTilePanel::DragLeave( void )
{
	if (m_pDropTargetHelper)
		m_pDropTargetHelper->DragLeave();
	m_bDropValid=false;
	m_InsertIndex=-1;
	if (m_hWnd) Invalidate();
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CTilePanel::Drop( IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect )
{
	POINT p={pt.x,pt.y};
	DWORD effect=m_bDropValid?GetTileDropEffect(*pdwEffect):DROPEFFECT_NONE;
	if (m_pDropTargetHelper)
		m_pDropTargetHelper->Drop(pDataObj,&p,effect);
	int insertIndex=m_InsertIndex;
	bool bValid=m_bDropValid;
	m_bDropValid=false;
	m_InsertIndex=-1;
	*pdwEffect=DROPEFFECT_NONE;

	if (bValid && pDataObj)
	{
		CComPtr<IShellItemArray> pArray;
		if (SUCCEEDED(SHCreateShellItemArrayFromDataObject(pDataObj,IID_IShellItemArray,(void**)&pArray)))
		{
			std::vector<CString> fileNames;
			DWORD count=0;
			pArray->GetCount(&count);
			for (DWORD i=0;i<count;i++)
			{
				CComPtr<IShellItem> pItem;
				if (FAILED(pArray->GetItemAt(i,&pItem)))
					continue;
				PIDLIST_ABSOLUTE pidl=NULL;
				if (FAILED(SHGetIDListFromObject(pItem,&pidl)) || !pidl)
					continue;
				CComString pName;
				pItem->GetDisplayName(SIGDN_NORMALDISPLAY,&pName);
				CString fileName;
				if (CreateTileLink(pidl,pName?(const wchar_t*)pName:L"",fileName))
					fileNames.push_back(fileName);
				ILFree(pidl);
			}
			if (!fileNames.empty())
			{
				InsertNewTiles(fileNames,insertIndex);
				*pdwEffect=effect;
			}
		}
	}
	if (m_hWnd) Invalidate();
	return S_OK;
}
