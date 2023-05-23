//
//  Signer.hpp
//  AltSign-Windows
//
//

#ifndef Signer_hpp
#define Signer_hpp

/* The classes below are exported */
#pragma GCC visibility push(default)

#include <string>
#include <vector>

#include "Team.hpp"
#include "Certificate.hpp"
#include "ProvisioningProfile.hpp"

class Signer
{
public:
    Signer(std::shared_ptr<Certificate> certificate);
    ~Signer();
    
    std::shared_ptr<Team> team() const;
    std::shared_ptr<Certificate> certificate() const;
    
    void SignApp(std::string appPath, std::vector<std::shared_ptr<ProvisioningProfile>> profiles, std::map<std::string, std::string> customEntitlements);
    
private:
    std::shared_ptr<Team> _team;
    std::shared_ptr<Certificate> _certificate;
};

#pragma GCC visibility pop

#endif /* Signer_hpp */
