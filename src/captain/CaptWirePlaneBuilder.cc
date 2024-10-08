#include "CaptWirePlaneBuilder.hh"
#include "EDepSimBuilder.hh"

#include "EDepSimLog.hh"

#include <globals.hh>
#include <G4Material.hh>
#include <G4LogicalVolume.hh>
#include <G4VPhysicalVolume.hh>
#include <G4PVPlacement.hh>
#include <G4VisAttributes.hh>

#include <G4SystemOfUnits.hh>
#include <G4PhysicalConstants.hh>

#include <G4Polyhedra.hh>
#include <G4Box.hh>
#include <G4Tubs.hh>

#include <cmath>

class CaptWirePlaneMessenger
    : public EDepSim::BuilderMessenger {
private:
    CaptWirePlaneBuilder* fBuilder;
    G4UIcmdWithADoubleAndUnit* fApothemCMD;
    G4UIcmdWithADoubleAndUnit* fSpacingCMD;
    G4UIcmdWithAnInteger* fMaxWireCountCMD;
   
    
public:
    CaptWirePlaneMessenger(CaptWirePlaneBuilder* c) 
        : EDepSim::BuilderMessenger(c,"Control the drift region geometry."),
          fBuilder(c) {

        fApothemCMD
            = new G4UIcmdWithADoubleAndUnit(CommandName("apothem"),this);
        fApothemCMD->SetGuidance("Set the apothem of the drift region.");
        fApothemCMD->SetParameterName("apothem",false);
        fApothemCMD->SetUnitCategory("Length");

        fSpacingCMD = new G4UIcmdWithADoubleAndUnit(
            CommandName("wireSpacing"),this);
        fSpacingCMD->SetGuidance(
            "Set the wire spacing.");
        fSpacingCMD->SetParameterName("spacing",false);
        fSpacingCMD->SetUnitCategory("Length");

        fMaxWireCountCMD = new G4UIcmdWithAnInteger(
            CommandName("maxWireCount"),this);
        fMaxWireCountCMD->SetGuidance(
            "Set the maximum number of wires in a plane.");
        fMaxWireCountCMD->SetParameterName("count",false);

    }

    virtual ~CaptWirePlaneMessenger() {
        delete fApothemCMD;
        delete fSpacingCMD;
        delete fMaxWireCountCMD;
    }

    void SetNewValue(G4UIcommand *cmd, G4String val) {
        if (cmd==fApothemCMD) {
            fBuilder->SetApothem(fApothemCMD->GetNewDoubleValue(val));
        }
        else if (cmd==fSpacingCMD) {
            fBuilder->SetSpacing(fSpacingCMD->GetNewDoubleValue(val));
        }
        else if (cmd==fMaxWireCountCMD) {
            fBuilder->SetMaxWireCount(fMaxWireCountCMD->GetNewIntValue(val));
        }
        else {
            EDepSim::BuilderMessenger::SetNewValue(cmd,val);
        }
    }

};

void CaptWirePlaneBuilder::Init(void) {
    SetMessenger(new CaptWirePlaneMessenger(this));
    SetApothem(1000*mm); 
    SetSpacing(3*mm);
    SetHeight(1*mm);
    SetMaxWireCount(0);   // default value used to see if limit is set.
    SetSensitiveDetector("drift","segment");
    SetMaximumHitLength(1*mm);
    SetMaximumHitSagitta(0.5*CLHEP::mm);
}

CaptWirePlaneBuilder::~CaptWirePlaneBuilder() {}

G4LogicalVolume *CaptWirePlaneBuilder::GetPiece(void) {

    double rInner[] = {0.0, 0.0};
    double rOuter[] = {GetApothem(), GetApothem()};
    double zPlane[] = {-GetHeight()/2, GetHeight()/2};

    G4LogicalVolume *logVolume
	= new G4LogicalVolume(new G4Polyhedra(GetName(),
                                              90*CLHEP::degree, 450*CLHEP::degree,
                                              6, 2,
                                              zPlane, rInner, rOuter),
                              FindMaterial("Argon_Liquid"),
                              GetName());
    logVolume->SetVisAttributes(GetColor(FindMaterial("Captain_Wire"), 0.2));

    if (GetSensitiveDetector()) {
        logVolume->SetSensitiveDetector(GetSensitiveDetector());
    }

    // Determine the maximum (possible) wire length.
    double maxLength = 2*GetApothem()/std::cos(30*CLHEP::degree);

    // Determine the number of wires.  There is always at least one wire.
    int wires = int (2.0*GetApothem()/GetSpacing());
    if (wires<1) wires = 1;

    if (GetMaxWireCount()>0 && GetMaxWireCount()<wires) {
        EDepSimLog("Reduce number of wires for " << GetName()
                << " from " << wires
                << " to " << GetMaxWireCount());
        wires = GetMaxWireCount();
    }

    EDepSimLog("Construct " << GetName() 
            << " with " << wires << " wires"
            << "  (spacing: " << GetSpacing()/CLHEP::mm << " mm)");

    if (wires<GetMaxWireCount()) {
        EDepSimWarn(GetName() << ": Wire count is less than expected:"
                    << "  " << wires
                    << " < " << GetMaxWireCount());
    }

    G4RotationMatrix* wireRotation = NULL;

    // The core needs to be rotated into the wire volume.  The core is not
    // used inside the geometry.
    G4RotationMatrix* coreRotation = new G4RotationMatrix(); 
    coreRotation->rotateX(90*CLHEP::degree);

    // The offset of the first wire.
    double baseOffset = - 0.5*GetSpacing()*(wires-1);
    for (int wire = 0; wire<wires; ++wire) {
        // The position of the wire.
        double wireOffset = baseOffset + wire*GetSpacing();
        double wireLength = maxLength-2*std::abs(wireOffset)*std::tan(30*CLHEP::degree);
        double wireDiameter = std::min(GetHeight()/2, GetSpacing()/4);

        // Apply a correction to the length account for the corners of the
        // wire box.
        wireLength -= 2*GetSpacing()*std::cos(30*CLHEP::degree);

        G4LogicalVolume* logWire 
            = new G4LogicalVolume(new G4Box(GetName()+"/Wire",
                                            GetSpacing()/2,
                                            wireLength/2,
                                            GetHeight()/2),
                                  FindMaterial("Argon_Liquid"),
                                  GetName()+"/Wire");
        logWire->SetVisAttributes(G4VisAttributes::GetInvisible());

        if (GetSensitiveDetector()) {
            logWire->SetSensitiveDetector(GetSensitiveDetector());
        }

        new G4PVPlacement(wireRotation,                  // rotation.
                          G4ThreeVector(wireOffset,0,0), // position
                          logWire,                       // logical volume
                          logWire->GetName(),            // name
                          logVolume,                     // mother  volume
                          false,                         // (not used)
                          0,                             // Copy number (zero)
                          false);                         // Check overlaps.

        G4LogicalVolume* logCore 
            = new G4LogicalVolume(new G4Tubs(GetName()+"/Wire/Core",
                                             0.0, wireDiameter/2.0,
                                             wireLength/2,
                                             0*CLHEP::degree, 360*CLHEP::degree),
                                  FindMaterial("Captain_Wire"),
                                  GetName()+"/Wire/Core");
        // Draw this as if it was a wire plane.
        logCore->SetVisAttributes(GetColor(logCore));
    
        new G4PVPlacement(coreRotation,         // rotation.
                          G4ThreeVector(0,0,0), // position
                          logCore,              // logical volume
                          logCore->GetName(),   // name
                          logWire,              // mother  volume
                          false,                // (not used)
                          0,                    // Copy number (zero)
                          false);               // Check overlaps.

        

    }

    return logVolume;
}
