#ifndef AGENT_KEYCHAIN_H
#define AGENT_KEYCHAIN_H

#include <string>

/**
 * Cross-platform secure credential storage.
 * Uses macOS Keychain Services on macOS.
 */
class AGENT_KEYCHAIN
{
public:
    AGENT_KEYCHAIN();
    ~AGENT_KEYCHAIN();

    /**
     * Store a credential in the secure storage.
     * @param aService Service identifier (e.g., "kicad.agent.openai")
     * @param aAccount Account/username (e.g., "api_key")
     * @param aPassword The secret value to store
     * @return True if successful
     */
    bool SetPassword( const std::string& aService, const std::string& aAccount, const std::string& aPassword );

    /**
     * Retrieve a credential from secure storage.
     * @param aService Service identifier
     * @param aAccount Account/username
     * @param aPassword Output parameter for the retrieved value
     * @return True if found and retrieved successfully
     */
    bool GetPassword( const std::string& aService, const std::string& aAccount, std::string& aPassword );

    /**
     * Delete a credential from secure storage.
     * @param aService Service identifier
     * @param aAccount Account/username
     * @return True if deleted successfully (or didn't exist)
     */
    bool DeletePassword( const std::string& aService, const std::string& aAccount );
};

#endif // AGENT_KEYCHAIN_H
