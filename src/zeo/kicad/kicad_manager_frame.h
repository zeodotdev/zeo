/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2013 CERN (www.cern.ch)
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 * Copyright (C) 2026, Zeo <team@zeo.dev>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#ifndef KICAD_H
#define KICAD_H

#include <kiway_player.h>

#include <vector>

class ACTION_TOOLBAR;
class BITMAP_BUTTON;
class EDA_BASE_FRAME;
class KICAD_SETTINGS;
class PLUGIN_CONTENT_MANAGER;
class PROJECT_TREE;
class PROJECT_TREE_PANE;
class LOCAL_HISTORY_PANE;
class UPDATE_MANAGER;
class SESSION_MANAGER;

/**
 * The main KiCad project manager frame.  It is not a KIWAY_PLAYER.
 */
class KICAD_MANAGER_FRAME : public EDA_BASE_FRAME
{
public:
    KICAD_MANAGER_FRAME( wxWindow* parent, const wxString& title,
                         const wxPoint& pos, const wxSize& size );

    ~KICAD_MANAGER_FRAME();

    void OnIdle( wxIdleEvent& event );

    bool canCloseWindow( wxCloseEvent& aCloseEvent ) override;
    void doCloseWindow() override;
    void OnSize( wxSizeEvent& event ) override;

    void UnarchiveFiles();
    void RestoreLocalHistory();
    void RestoreCommitFromHistory( const wxString& aHash );
    void ToggleLocalHistory();
    bool HistoryPanelShown();

    void OnOpenFileInTextEditor( wxCommandEvent& event );
    void OnEditAdvancedCfg( wxCommandEvent& event );

    void OnFileHistory( wxCommandEvent& event );
    void OnClearFileHistory( wxCommandEvent& aEvent );
    void OnExit( wxCommandEvent& event );

    /** Create the status line (like a wxStatusBar). This is actually a KISTATUSBAR status bar.
     * the specified number of fields is the extra number of fields, not the full field count.
     * @return a KISTATUSBAR (derived from wxStatusBar)
     */
    wxStatusBar* OnCreateStatusBar( int number, long style, wxWindowID id,
                                    const wxString& name ) override;

    /**
     * Hides the tabs for Editor notebook if there is only 1 page
     */
    void HideTabsIfNeeded();

    wxString GetCurrentFileName() const override;

    /**
     * @brief Creates a project and imports a non-KiCad Schematic and PCB
     * @param aWindowTitle to display to the user when opening the files
     * @param aFilesWildcard that includes both PCB and Schematic files (from
     * wildcards_and_files_ext.h)
     * @param aSchFileExtensions e.g. { "sch" } or { "csa" }. Specify { "INPUT" } to copy input file.
     * @param aPcbFileExtensions e.g. { "brd" } or { "cpa" }. Specify { "INPUT" } to copy input file.
     * @param aSchFileType Type of Schematic File to import (from SCH_IO_MGR::SCH_FILE_T)
     * @param aPcbFileType Type of PCB File to import (from IO_MGR::PCB_FILE_T)
    */
    void ImportNonKiCadProject( const wxString& aWindowTitle, const wxString& aFilesWildcard,
                                const std::vector<std::string>& aSchFileExtensions,
                                const std::vector<std::string>& aPcbFileExtensions,
                                int aSchFileType, int aPcbFileType );

    /**
     * Open dialog to import Altium project files.
     */
    void OnImportAltiumProjectFiles( wxCommandEvent& event );

    /**
     *  Open dialog to import CADSTAR Schematic and PCB Archive files.
     */
    void OnImportCadstarArchiveFiles( wxCommandEvent& event );

    /**
     *  Open dialog to import Eagle schematic and board files.
     */
    void OnImportEagleFiles( wxCommandEvent& event );

    /**
     *  Open dialog to import EasyEDA Std schematic and board files.
     */
    void OnImportEasyEdaFiles( wxCommandEvent& event );

    /**
     *  Open dialog to import EasyEDA Pro schematic and board files.
     */
    void OnImportEasyEdaProFiles( wxCommandEvent& event );

    /**
     *  Open dialog to import PADS Logic schematic and PCB files.
     */
    void OnImportPadsProjectFiles( wxCommandEvent& event );

    /**
     *  Open dialog to import gEDA/gaf schematic and PCB files.
     */
    void OnImportGedaFiles( wxCommandEvent& event );

    /**
     * Prints the current working directory name and the project name on the text panel.
     */
    void PrintPrjInfo();

    void RefreshProjectTree();

    /**
     * Creates a new project by setting up and initial project, schematic, and board files.
     *
     * The project file is copied from the kicad.pro template file if possible.  Otherwise,
     * a minimal project file is created from an empty project.  A minimal schematic and
     * board file are created to prevent the schematic and board editors from complaining.
     * If any of these files already exist, they are not overwritten.
     *
     * @param aProjectFileName is the absolute path of the project file name.
     * @param aCreateStubFiles specifies if an empty PCB and schematic should be created
     */
    void CreateNewProject( const wxFileName& aProjectFileName, bool aCreateStubFiles = true );

    /**
     * Closes the project, and saves it if aSave is true;
     */
    bool CloseProject( bool aSave );

    /**
     * Loads a new project
     * @param aProjectFileName is the path to the project to load
     * @return true if the project was successfully loaded
     */
    bool LoadProject( const wxFileName& aProjectFileName );

    /**
     * Load a multi-board container project (`.kicad_pro` with
     * `multi_board.container = true`) and switch the launcher into
     * multi-board mode.
     *
     * Loads the container, stores it on the frame so subsequent UI queries
     * can detect multi-board state, and then calls LoadProject() on the
     * first sub-project so the rest of the application has a usable PROJECT
     * context. If the container has no sub-projects, the Setup dialog is
     * shown and the first sub-project added there is loaded afterwards.
     *
     * @param aMultiProjectFile path to the container `.kicad_pro`
     * @return true on success
     */
    bool LoadMultiBoardProject( const wxFileName& aMultiProjectFile );

    /**
     * Switch the active sub-project within the current multi-board session.
     *
     * Calls LoadProject() on the selected sub-project's `.kicad_pro` while
     * preserving the multi-board container state, so the title, status bar,
     * and "Manage Sub-Boards…" action continue to reflect the multi-board
     * context.
     *
     * No-op (returns false) if the current session is not multi-board or the
     * UUID does not resolve to a known sub-project.
     *
     * @param aSubProjectUuid UUID of the sub-project to activate
     * @return true on success
     */
    bool SwitchActiveSubProject( const KIID& aSubProjectUuid );

    /**
     * M4 spike: open a sub-project's schematic in a *new* SCH_EDIT_FRAME
     * peered alongside whatever other editors are currently open. The
     * container project stays loaded + active in SETTINGS_MANAGER; the
     * new editor's Prj() is pinned to the sub-project via
     * SetPrjOverride.
     *
     * Caveats (known, to be addressed in M4.3+):
     * - Bypasses Kiway::Player()'s one-per-frame-type cache; cross-
     *   probing via ExpressMail will only hit the first eeschema frame.
     * - Closing this frame does not clean up the loaded sub-project
     *   PROJECT from SETTINGS_MANAGER.
     */
    bool SpawnPeerSchematicEditor( const KIID& aSubProjectUuid );

    /**
     * Spawn a dedicated PCB editor frame bound to the given sub-project,
     * mirroring SpawnPeerSchematicEditor for .kicad_pcb files.
     */
    bool SpawnPeerPcbEditor( const KIID& aSubProjectUuid );

    /**
     * @return the PROJECT_FILE of the current session if it's a multi-board
     * container, or nullptr for plain single-board projects.
     */
    class PROJECT_FILE* GetMultiBoardProject() const;

    void OpenJobsFile( const wxFileName& aFileName, bool aCreate = false,
                       bool aResaveProjectPreferences = true );

    /**
     * Detach a closing JOBSET_FRAME from the tracking list and persist
     * the updated open-jobsets list. Invoked by JOBSET_FRAME::onClose.
     */
    void NotifyJobsetFrameClosing( class JOBSET_FRAME* aFrame );

    /**
     * Close every standalone jobset window owned by this manager,
     * honoring each panel's GetCanClose() gate. Returns true if all
     * frames consented to close, false if any of them vetoed (typically
     * the user picked Cancel on an unsaved-changes prompt).
     */
    bool CloseAllJobsetFrames();

    /// Number of standalone jobset windows currently open for this
    /// session. Used by the UI condition for "View Jobsets" so the menu
    /// item enables only when there's something to surface.
    size_t OpenJobsetFrameCount() const;


    void LoadSettings( APP_SETTINGS_BASE* aCfg ) override;

    void SaveSettings( APP_SETTINGS_BASE* aCfg ) override;

    void ShowChangedLanguage() override;
    void CommonSettingsChanged( int aFlags ) override;
    void ProjectChanged() override;

    void PreloadAllLibraries();

    /**
     * Called by sending a event with id = ID_INIT_WATCHED_PATHS
     * rebuild the list of watched paths
     */
    void OnChangeWatchedPaths( wxCommandEvent& aEvent );

    const wxString GetProjectFileName() const;

    bool IsProjectActive();
    // read only accessors
    const wxString SchFileName();
    const wxString SchLegacyFileName();
    const wxString PcbFileName();
    const wxString PcbLegacyFileName();

    void ReCreateTreePrj();

    /**
     * @param aIsExplicitUserSave is true to indicate the user ran a Save Project action explicitly
     *        Note that this parameter should currently *always* be false, because there is no
     *        explicit Save Project action in the project manager.  This means that anytime the
     *        project manager saves project local settings, it is an implicit save (and should not
     *        actually save the file if it was migrated)
     */
    void SaveOpenJobSetsToLocalSettings( bool aIsExplicitUserSave = false );

    wxWindow* GetToolCanvas() const override;

    std::shared_ptr<PLUGIN_CONTENT_MANAGER> GetPcm() { return m_pcm; };

    void SetPcmButton( BITMAP_BUTTON* aButton );

    void CreatePCM();   // creates the PLUGIN_CONTENT_MANAGER

    /**
     * Run a Zeo update check. Called automatically on launcher startup and manually
     * via the Help > Check for Updates menu item.
     *
     * @param aManual true if user-initiated. Manual checks bypass the "skip this
     *                version" suppression and surface every outcome (up-to-date,
     *                network error) as a message box instead of silently no-op-ing.
     */
    void RunUpdateCheck( bool aManual );

    // Used only on Windows: stores the info message about file watcher
    wxString m_FileWatcherInfo;

    DECLARE_EVENT_TABLE()

protected:
    virtual void setupUIConditions() override;

    void doReCreateMenuBar() override;

    void onToolbarSizeChanged();

    void onNotebookPageCloseRequest( wxAuiNotebookEvent& evt );

    void onNotebookPageCountChanged( wxAuiNotebookEvent& evt );

private:
    void setupTools();
    void setupActions();

    void DoWithAcceptedFiles() override;

    APP_SETTINGS_BASE* config() const override;

    KICAD_SETTINGS* kicadSettings() const;

    const SEARCH_STACK& sys_search() override;

    wxString help_name() override;

    void updatePcmButtonBadge();

private:
    bool                  m_openSavedWindows;
    bool                  m_restoredFromHistory;  ///< Set after restore to mark editors dirty
    int                   m_leftWinWidth;
    bool                  m_active_project;
    bool                  m_showHistoryPanel;

    PROJECT_TREE_PANE*    m_projectTreePane;
    LOCAL_HISTORY_PANE*   m_historyPane;
    wxAuiNotebook*        m_notebook;
    int                   m_lastToolbarIconSize;

    /// Live standalone JOBSET_FRAME windows. Maintained by OpenJobsFile
    /// (insert) and NotifyJobsetFrameClosing (erase); never owns the
    /// frames — they auto-destroy on close.
    std::vector<class JOBSET_FRAME*> m_jobsetFrames;

    std::shared_ptr<PLUGIN_CONTENT_MANAGER> m_pcm;
    BITMAP_BUTTON*                          m_pcmButton;
    int                                     m_pcmUpdateCount;
    std::unique_ptr<UPDATE_MANAGER>         m_updateManager;
    std::unique_ptr<SESSION_MANAGER>        m_sessionManager;

    /// Multi-board container PROJECT currently open as the launcher's
    /// "session", if any. Stored separately from Prj() because spawning
    /// a peer PCB/schematic editor can swap the SETTINGS_MANAGER active
    /// project to the sub-project; the launcher still needs to know the
    /// container for actions like Manage Sub-Boards or Edit Multi-Board
    /// Schematic. Nulled automatically via PROJECT::AddDestroyHook if
    /// the container is unloaded.
    PROJECT* m_multiBoardContainer = nullptr;

    /// Swap m_multiBoardContainer, managing destroy-hook (re)registration.
    void setMultiBoardContainer( PROJECT* aContainer );

public:
    SESSION_MANAGER* GetSessionManager() const { return m_sessionManager.get(); }
};


// The C++ project manager includes a single PROJECT in its link image.
class PROJECT;
extern PROJECT& Prj();

#endif
