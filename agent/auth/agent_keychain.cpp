#include "agent_keychain.h"
#include <wx/log.h>

#ifdef __APPLE__
#include <Security/Security.h>
#endif

AGENT_KEYCHAIN::AGENT_KEYCHAIN()
{
}

AGENT_KEYCHAIN::~AGENT_KEYCHAIN()
{
}

bool AGENT_KEYCHAIN::SetPassword( const std::string& aService, const std::string& aAccount, const std::string& aPassword )
{
#ifdef __APPLE__
    // First try to delete existing item (update by delete + add)
    DeletePassword( aService, aAccount );

    OSStatus status = SecKeychainAddGenericPassword(
        nullptr, // default keychain
        aService.length(),
        aService.c_str(),
        aAccount.length(),
        aAccount.c_str(),
        aPassword.length(),
        aPassword.c_str(),
        nullptr // no item reference needed
    );

    if( status == errSecSuccess )
    {
        wxLogTrace( "Agent", "Keychain: Stored credential for %s/%s", aService.c_str(), aAccount.c_str() );
        return true;
    }
    else
    {
        wxLogTrace( "Agent", "Keychain: Failed to store credential (error %d)", status );
        return false;
    }
#else
    wxLogWarning( "Keychain storage not implemented for this platform" );
    return false;
#endif
}

bool AGENT_KEYCHAIN::GetPassword( const std::string& aService, const std::string& aAccount, std::string& aPassword )
{
#ifdef __APPLE__
    void*    passwordData = nullptr;
    UInt32   passwordLength = 0;

    OSStatus status = SecKeychainFindGenericPassword(
        nullptr, // default keychain
        aService.length(),
        aService.c_str(),
        aAccount.length(),
        aAccount.c_str(),
        &passwordLength,
        &passwordData,
        nullptr // no item reference needed
    );

    if( status == errSecSuccess && passwordData )
    {
        aPassword = std::string( static_cast<const char*>( passwordData ), passwordLength );
        SecKeychainItemFreeContent( nullptr, passwordData );
        return true;
    }
    else if( status == errSecItemNotFound )
    {
        wxLogTrace( "Agent", "Keychain: Credential not found for %s/%s", aService.c_str(), aAccount.c_str() );
        return false;
    }
    else
    {
        wxLogTrace( "Agent", "Keychain: Failed to retrieve credential (error %d)", status );
        return false;
    }
#else
    wxLogWarning( "Keychain storage not implemented for this platform" );
    return false;
#endif
}

bool AGENT_KEYCHAIN::DeletePassword( const std::string& aService, const std::string& aAccount )
{
#ifdef __APPLE__
    SecKeychainItemRef itemRef = nullptr;

    OSStatus status = SecKeychainFindGenericPassword(
        nullptr, // default keychain
        aService.length(),
        aService.c_str(),
        aAccount.length(),
        aAccount.c_str(),
        nullptr,
        nullptr,
        &itemRef
    );

    if( status == errSecSuccess && itemRef )
    {
        status = SecKeychainItemDelete( itemRef );
        CFRelease( itemRef );
        return ( status == errSecSuccess );
    }
    else if( status == errSecItemNotFound )
    {
        // Not found is considered success for deletion
        return true;
    }
    else
    {
        return false;
    }
#else
    return false;
#endif
}
