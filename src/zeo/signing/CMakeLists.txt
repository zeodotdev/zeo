if (APPLE AND KICAD_OSX_CODESIGN )
    install( CODE "
        include( ${KICAD_CMAKE_MODULE_PATH}/InstallSteps/SignMacOS.cmake )

        # InstallMacOS.cmake pulls in all the dynamic dependencies of Python into the Python framework dir.
        # We don't want this because it makes signing fail, and they are not needed either.
        # I can't figure out how to keep it from happening, so for now, just clean them up
        message( STATUS \"Cleaning up Python.framework...\" )
        file( GLOB _PYTHON_TO_REMOVE ${OSX_BUNDLE_INSTALL_LIB_DIR}/Python.framework/* )
        file( REMOVE \${_PYTHON_TO_REMOVE} )

        # Recreate essential Python.framework symlinks destroyed by the cleanup above.
        # Without these, codesign rejects the framework as invalid bundle format.
        set( _pyfw \"${OSX_BUNDLE_INSTALL_LIB_DIR}/Python.framework\" )
        if( IS_DIRECTORY \"\${_pyfw}/Versions/3.10\" )
            message( STATUS \"Recreating Python.framework symlinks (3.10)...\" )
            execute_process( COMMAND ln -sfn 3.10 \"\${_pyfw}/Versions/Current\" )
            execute_process( COMMAND ln -sfn Versions/Current/Headers \"\${_pyfw}/Headers\" )
            execute_process( COMMAND ln -sfn Versions/Current/Python \"\${_pyfw}/Python\" )
            execute_process( COMMAND ln -sfn Versions/Current/Resources \"\${_pyfw}/Resources\" )
        endif()

        message( STATUS \"Signing bundles...\" )
        sign_kicad_bundle( \"${OSX_BUNDLE_INSTALL_DIR}\" \"\${KICAD_OSX_SIGNING_ID}\" \"\${KICAD_OSX_SIGNING_USE_SECURE_TIMESTAMP}\" \"\${KICAD_OSX_SIGNING_USE_HARDENED_RUNTIME}\" \"\${KICAD_OSX_SIGNING_ENTITLEMENTS_FILE}\" )
    " COMPONENT Runtime )
endif()
