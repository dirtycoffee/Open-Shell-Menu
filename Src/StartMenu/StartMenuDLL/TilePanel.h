// Open-Shell (c) 2017-2026, The Open-Shell Team

// TilePanel - an optional Windows 10-style tile panel attached to the side of the Windows 7-style main menu

#pragma once

#include "ItemManager.h"
#include <vector>

class CTilePanel: public CWindowImpl<CTilePanel>, public IDropTarget
{
public:
	CTilePanel( void );
	DECLARE_WND_CLASS_EX(L"OpenShell.CTilePanel",0,COLOR_MENU)

	// message handlers
	BEGIN_MSG_MAP( CTilePanel )
		MESSAGE_HANDLER( WM_CREATE, OnCreate )
		MESSAGE_HANDLER( WM_DESTROY, OnDestroy )
		MESSAGE_HANDLER( WM_MOUSEACTIVATE, OnMouseActivate )
		MESSAGE_HANDLER( WM_PAINT, OnPaint )
		MESSAGE_HANDLER( WM_PRINTCLIENT, OnPaint )
		MESSAGE_HANDLER( WM_ERASEBKGND, OnEraseBkgnd )
		MESSAGE_HANDLER( WM_SETCURSOR, OnSetCursor )
		MESSAGE_HANDLER( WM_MOUSEMOVE, OnMouseMove )
		MESSAGE_HANDLER( WM_MOUSELEAVE, OnMouseLeave )
		MESSAGE_HANDLER( WM_MOUSEWHEEL, OnMouseWheel )
		MESSAGE_HANDLER( WM_LBUTTONDOWN, OnLButtonDown )
		MESSAGE_HANDLER( WM_LBUTTONUP, OnLButtonUp )
		MESSAGE_HANDLER( WM_RBUTTONDOWN, OnRButtonDown )
		MESSAGE_HANDLER( WM_RBUTTONUP, OnRButtonUp )
		MESSAGE_HANDLER( WM_CAPTURECHANGED, OnCaptureChanged )
	END_MSG_MAP()

	// IUnknown (the panel is a static object, so the reference counting is a no-op)
	virtual HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void **ppvObject );
	virtual ULONG STDMETHODCALLTYPE AddRef( void ) { return 1; }
	virtual ULONG STDMETHODCALLTYPE Release( void ) { return 1; }

	// IDropTarget
	virtual HRESULT STDMETHODCALLTYPE DragEnter( IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect );
	virtual HRESULT STDMETHODCALLTYPE DragOver( DWORD grfKeyState, POINTL pt, DWORD *pdwEffect );
	virtual HRESULT STDMETHODCALLTYPE DragLeave( void );
	virtual HRESULT STDMETHODCALLTYPE Drop( IDataObject *pDataObj, DWORD grfKeyState, POINTL pt, DWORD *pdwEffect );

	// Creates the panel for the given main menu. Call only when the tile panel is enabled
	void CreatePanel( HWND menu );
	// Repositions the panel next to the visible part of the main menu (screen coordinates)
	void UpdatePosition( const RECT &menuRect, bool bMenuLeft, bool bVisible );
	void HidePanel( void );
	void RefreshIcons( void );
	// Creates a tile for the given item. Returns true if successful
	bool PinItem( PCIDLIST_ABSOLUTE pidl, const wchar_t *name, int insertIndex=-1 );

private:
	struct Tile
	{
		CString fileName; // name of the .lnk file in the tiles folder
		CString path; // full path to the .lnk file
		CString name; // display name
		const CItemManager::ItemInfo *pInfo;
	};

	enum TDragMode
	{
		DRAG_NONE,
		DRAG_RESIZE, // resizing the panel with the grip
		DRAG_PENDING, // a tile is pressed but the mouse hasn't moved enough
		DRAG_REORDER, // a tile is being dragged to a new position
	};

	HWND m_Owner; // the main menu window
	CString m_Folder; // the tiles folder
	std::vector<Tile> m_Tiles;
	int m_Width; // current width in pixels
	int m_TileSize, m_Gap, m_Margin, m_Grip; // metrics in pixels
	bool m_bRightSide; // the panel is on the right side of the menu (the grip is on the right)
	bool m_bRTL;
	RECT m_MenuRect; // last known visible menu rect
	RECT m_WorkArea;
	int m_ScrollPos;
	int m_HotTile;
	bool m_bTrackMouse;
	HFONT m_Font;

	TDragMode m_DragMode;
	int m_DragTile;
	POINT m_DragStart; // screen coordinates
	int m_DragStartWidth;
	int m_InsertIndex; // insert position while reordering or dropping (-1 - none)
	bool m_bInsertAtRowEnd; // the insert mark is after the last tile of a row

	CComPtr<IDropTargetHelper> m_pDropTargetHelper;
	bool m_bDropValid;

	LRESULT OnCreate( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnDestroy( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnMouseActivate( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled ) { return MA_NOACTIVATE; }
	LRESULT OnPaint( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnEraseBkgnd( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled ) { return 1; }
	LRESULT OnSetCursor( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnMouseMove( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnMouseLeave( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnMouseWheel( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnLButtonDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnLButtonUp( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnRButtonDown( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled ) { return 0; }
	LRESULT OnRButtonUp( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );
	LRESULT OnCaptureChanged( UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled );

	void LoadTiles( void );
	void SaveOrder( void );
	int GetColumns( void ) const;
	int GetMinWidth( void ) const;
	int GetMaxWidth( void ) const;
	int GetContentHeight( void ) const;
	void ClampScroll( void );
	void GetTileRect( int index, RECT &rc ) const;
	void GetGripRect( RECT &rc ) const;
	int HitTestTile( POINT pt ) const;
	int GetInsertIndex( POINT pt, bool *pbRowEnd ) const;
	void ApplyPosition( void );
	void SetWidth( int width );
	void SaveWidth( void );
	void SetHotTile( int index );
	void LaunchTile( int index );
	void ShowTileMenu( int index, POINT pt );
	void EndDrag( bool bCancel );
	bool EnsureFolder( void );
	void InsertNewTiles( const std::vector<CString> &fileNames, int insertIndex );
	bool CreateTileLink( PCIDLIST_ABSOLUTE pidl, const wchar_t *name, CString &fileName );
	void Draw( HDC hdc, const RECT &clientRect );
};

extern CTilePanel g_TilePanel;
