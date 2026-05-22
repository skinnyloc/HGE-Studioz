/**********************************************************************

 HGE Music Studio — Plugin Validator

 Validates bundled plugins for:
   - Mach-O binary integrity (magic bytes)
   - Architecture compatibility (Intel/Apple Silicon/Universal)
   - Code signing status
   - Bundle structure correctness
   - 32-bit vs 64-bit detection

 Designed to catch:
   - Corrupted downloads
   - Architecture mismatches on Apple Silicon Macs
   - Unsigned/malicious plugins
   - Incomplete bundles

 **********************************************************************/

#ifndef __HGE_PLUGIN_VALIDATOR_H__
#define __HGE_PLUGIN_VALIDATOR_H__

#include "PluginManagerModule.h"
#include <wx/string.h>

struct ValidationResult
{
   bool     valid;
   wxString arch;        // "x86_64", "arm64", "universal", "unknown"
   wxString error;       // Empty if valid
   int      warningCount;
   wxString warnings;    // Concatenated warnings
   bool     isSigned;    // Has valid code signature
};

class PluginValidator
{
public:
   static PluginValidator &Get();

   ValidationResult Validate(const PluginBundle &bundle);
   ValidationResult ValidateBinary(const wxString &binaryPath);

   bool   IsArchitectureCompatible(const wxString &arch);
   wxString DetectArchitecture(const wxString &binaryPath);

private:
   PluginValidator() = default;
   bool CheckMachO(const wxString &path, wxString &error);
};

#endif // __HGE_PLUGIN_VALIDATOR_H__
