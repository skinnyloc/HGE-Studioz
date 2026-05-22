/**********************************************************************

 HGE Music Studio — Plugin Validator Implementation

 Validates bundled plugins for Mach-O integrity, architecture
 compatibility, and code signing. Designed to catch corrupted
 downloads and architecture mismatches before they crash the app.

 **********************************************************************/

#include "PluginValidator.h"

#include <wx/file.h>
#include <wx/log.h>
#include <wx/filename.h>

#include <fcntl.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/loader.h>
#include <mach-o/fat.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

PluginValidator &PluginValidator::Get()
{
   static PluginValidator instance;
   return instance;
}

ValidationResult PluginValidator::Validate(const PluginBundle &bundle)
{
   ValidationResult result;
   result.valid = false;
   result.warningCount = 0;

   // For bundles, find and validate the actual binary
   if (bundle.type == wxT("VST2") || bundle.type == wxT("VST3") || bundle.type == wxT("AU"))
   {
      wxString binaryPath;
      wxString plistPath = bundle.path + wxFILE_SEP_PATH + wxT("Contents/Info.plist");
      if (wxFile::Exists(plistPath))
      {
         // Read binary name from Info.plist
         wxString binaryName = PluginManagerModule::Get().ReadInfoPlistValue(
            plistPath, wxT("CFBundleExecutable"));
         if (!binaryName.IsEmpty())
            binaryPath = bundle.path + wxFILE_SEP_PATH +
                         wxT("Contents/MacOS/") + binaryName;
      }

      if (binaryPath.IsEmpty() || !wxFile::Exists(binaryPath))
      {
         result.error = wxT("Binary not found in bundle");
         return result;
      }

      return ValidateBinary(binaryPath);
   }

   if (bundle.type == wxT("Nyquist"))
   {
      result.valid = true;
      result.arch = wxT("script");
      return result;
   }

   // Default: assume valid
   result.valid = true;
   result.arch = wxT("unknown");
   return result;
}

ValidationResult PluginValidator::ValidateBinary(const wxString &binaryPath)
{
   ValidationResult result;
   result.valid = false;
   result.warningCount = 0;
   result.isSigned = false;

   if (!wxFile::Exists(binaryPath))
   {
      result.error = wxT("File not found");
      return result;
   }

   // Open binary and check header
   int fd = open(binaryPath.utf8_str(), O_RDONLY);
   if (fd < 0)
   {
      result.error = wxT("Cannot open file");
      return result;
   }

   uint32_t magic;
   if (read(fd, &magic, sizeof(magic)) != sizeof(magic))
   {
      close(fd);
      result.error = wxT("Cannot read header");
      return result;
   }

   switch (magic)
   {
      case FAT_MAGIC:
      case FAT_CIGAM:
      case FAT_MAGIC_64:
      case FAT_CIGAM_64:
      {
         // Universal binary
         lseek(fd, 0, SEEK_SET);
         struct fat_header fh;
         read(fd, &fh, sizeof(fh));

         uint32_t narch = (magic == FAT_MAGIC || magic == FAT_CIGAM)
            ? OSSwapBigToHostInt32(fh.nfat_arch)
            : fh.nfat_arch;

         bool hasX86 = false, hasArm = false;
         for (uint32_t i = 0; i < narch; i++)
         {
            struct fat_arch fa;
            if (read(fd, &fa, sizeof(fa)) != sizeof(fa)) break;
            cpu_type_t cpu = OSSwapBigToHostInt32(fa.cputype);
            if (cpu == CPU_TYPE_X86_64) hasX86 = true;
            if (cpu == CPU_TYPE_ARM64)   hasArm = true;
         }

         close(fd);

         if (hasX86 && hasArm)
            result.arch = wxT("universal (Intel + Apple Silicon)");
         else if (hasArm)
            result.arch = wxT("arm64 (Apple Silicon)");
         else if (hasX86)
         {
            result.arch = wxT("x86_64 (Intel)");
            // Warn on Intel-only for Apple Silicon Macs
#ifdef __arm64__
            result.warningCount++;
            result.warnings += wxT("Intel-only binary on Apple Silicon Mac. ")
                              wxT("Rosetta 2 required.\n");
#endif
         }

         result.valid = true;
         break;
      }

      case MH_MAGIC:
      case MH_CIGAM:
      {
         close(fd);
         result.arch = wxT("i386 (32-bit)");
         result.warningCount++;
         result.warnings = wxT("32-bit binary detected. May not be compatible.\n");
         result.valid = true; // Allow but warn
         break;
      }

      case MH_MAGIC_64:
      case MH_CIGAM_64:
      {
         lseek(fd, 0, SEEK_SET);
         struct mach_header_64 mh;
         read(fd, &mh, sizeof(mh));
         close(fd);

         if (mh.cputype == CPU_TYPE_ARM64)
            result.arch = wxT("arm64");
         else if (mh.cputype == CPU_TYPE_X86_64)
            result.arch = wxT("x86_64");
         else
            result.arch = wxT("Unknown 64-bit");

         result.valid = true;
         break;
      }

      default:
      {
         close(fd);
         result.error = wxT("Not a valid Mach-O binary (magic: 0x")
                      + wxString::Format(wxT("%08x"), magic) + wxT(")");
         return result;
      }
   }

   // Code signing check (macOS only)
#ifdef __APPLE__
   SecStaticCodeRef staticCode = nullptr;
   CFURLRef url = CFURLCreateFromFileSystemRepresentation(
      kCFAllocatorDefault,
      (const UInt8 *)binaryPath.utf8_str(),
      binaryPath.utf8_str().length(),
      false);

   if (url)
   {
      if (SecStaticCodeCreateWithPath(url, kSecCSDefaultFlags, &staticCode) == errSecSuccess)
      {
         OSStatus status = SecStaticCodeCheckValidityWithErrors(
            staticCode,
            kSecCSDefaultFlags | kSecCSConsiderExpiration,
            nullptr, nullptr);

         if (status == errSecSuccess)
            result.isSigned = true;
         else if (status != errSecCSUnsigned)
         {
            result.warningCount++;
            result.warnings += wxString::Format(
               wxT("Code signing warning (status: %d)\n"), (int)status);
         }
         CFRelease(staticCode);
      }
      CFRelease(url);
   }
#endif

   return result;
}

bool PluginValidator::IsArchitectureCompatible(const wxString &arch)
{
   if (arch.Contains(wxT("universal"))) return true;

#ifdef __arm64__
   if (arch.Contains(wxT("arm64"))) return true;
   // Intel plugins need Rosetta 2
   if (arch.Contains(wxT("x86_64"))) return true; // Rosetta 2 available
#else
   if (arch.Contains(wxT("x86_64"))) return true;
#endif

   return false;
}

wxString PluginValidator::DetectArchitecture(const wxString &binaryPath)
{
   return ValidateBinary(binaryPath).arch;
}
