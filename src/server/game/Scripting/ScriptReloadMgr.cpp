/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ScriptReloadMgr.h"
#include "Config.h"
#include "Errors.h"
#include "ScriptMgr.h"
#include "World.h"

#ifndef ACORE_API_USE_DYNAMIC_LINKING

// A statically linked server loads no script library, so there is no module
// reference any script could have come from.
std::shared_ptr<ModuleReference>
    ScriptReloadMgr::AcquireModuleReferenceOfContext(std::string const& /*context*/)
{
    WPAbort();
}

// Nothing is watched or loaded in a static build, so there is nothing to set up.
void ScriptReloadMgr::Initialize()
{
}

// A statically linked server never loads a script library, so it never has a
// change set waiting for a commit either.
void ScriptReloadMgr::Update()
{
}

// The settings only matter to a reloader that can load libraries; an operator
// who asked for hotswapping here only gets told why it does not happen.
void ScriptReloadMgr::LoadSettings()
{
    if (sWorld->getBoolConfig(CONFIG_HOTSWAP_ENABLED))
    {
        LOG_WARN("scripts.hotswap", "HotSwap.Enabled is set, but this server was built without "
            "dynamic linking, so no script library can be loaded.");
    }
}

// Returns the empty implemented ScriptReloadMgr
ScriptReloadMgr* ScriptReloadMgr::instance()
{
    static ScriptReloadMgr instance;
    return &instance;
}

#else

#include "BuiltInConfig.h"
#include "CryptoHash.h"
#include "GitRevision.h"
#include "Log.h"
#include "MPSCQueue.h"
#include "StartProcess.h"
#include "StringFormat.h"
#include "Timer.h"
#include "Util.h"
#include <efsw/efsw.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

#if AC_PLATFORM == AC_PLATFORM_WINDOWS
#  include <windows.h>
#else // Posix and Apple
#  include <dlfcn.h>
#endif

// Promote the sScriptReloadMgr to a HotSwapScriptReloadMgr
// in this compilation unit.
#undef sScriptReloadMgr
#define sScriptReloadMgr static_cast<HotSwapScriptReloadMgr*>(ScriptReloadMgr::instance())

// Returns "" on Windows and "lib" on posix.
static char const* GetSharedLibraryPrefix()
{
#if AC_PLATFORM == AC_PLATFORM_WINDOWS
    return "";
#else // Posix
    return "lib";
#endif
}

// Returns "dll" on Windows, "dylib" on OS X, and "so" on posix.
static char const* GetSharedLibraryExtension()
{
#if AC_PLATFORM == AC_PLATFORM_WINDOWS
    return "dll";
#elif AC_PLATFORM == AC_PLATFORM_APPLE
    return "dylib";
#else // Posix
    return "so";
#endif
}

#if AC_PLATFORM == AC_PLATFORM_WINDOWS
typedef HMODULE HandleType;
#else // Posix
typedef void* HandleType;
#endif

static fs::path GetDirectoryOfExecutable()
{
    ASSERT((!sConfigMgr->GetArguments().empty()),
        "Expected the arguments to contain at least 1 element!");

    fs::path path(sConfigMgr->GetArguments()[0]);
    if (path.is_absolute())
        return path.parent_path();

    std::error_code code;
    fs::path const absolute = fs::absolute(path, code);
    if (code)
        return path.parent_path();

    fs::path const canonical = fs::canonical(absolute, code);
    return (code ? absolute : canonical).parent_path();
}

/// Returns the given path resolved against the given base directory when it is
/// relative, the path itself otherwise.
static fs::path AbsolutePath(fs::path const& path, fs::path const& base)
{
    if (path.is_absolute())
        return path;

    std::error_code code;
    fs::path const absolute = fs::absolute(base / path, code);
    return code ? base / path : absolute;
}

/// Returns true when the given path is the base directory itself or lies below it.
static bool IsPathWithin(fs::path const& path, fs::path const& base)
{
    fs::path branch = path;
    while (!branch.empty())
    {
        if (branch == base)
            return true;

        fs::path const parent = branch.parent_path();
        if (parent == branch)
            break;

        branch = parent;
    }

    return false;
}

/// Unloads the shared library of the given handle as soon as the last reference
/// to it is dropped, and removes the cached copy the reloader loaded from.
class SharedLibraryUnloader
{
public:
    explicit SharedLibraryUnloader(fs::path path)
        : _path(std::move(path)) { }

    SharedLibraryUnloader(fs::path path, std::optional<fs::path> cachePath)
        : _path(std::move(path)), _cachePath(std::move(cachePath)) { }

    void operator()(HandleType handle) const
    {
        // Unload the associated shared library.
#if AC_PLATFORM == AC_PLATFORM_WINDOWS
        bool const success = (FreeLibrary(handle) != 0);
#else // Posix
        bool const success = (dlclose(handle) == 0);
#endif

        if (!success)
        {
            LOG_ERROR("scripts.hotswap", "Failed to unload (syscall) the shared library \"{}\".",
                _path.generic_string());

            return;
        }

        /// When the shared library was cached delete it's shared version
        if (_cachePath)
        {
            std::error_code error;
            if (!fs::remove(*_cachePath, error))
            {
                LOG_ERROR("scripts.hotswap", "Failed to delete the cached shared library \"{}\" ({}).",
                    _cachePath->generic_string(), error.message());

                return;
            }

            LOG_TRACE("scripts.hotswap", "Lazy unloaded the shared library \"{}\" "
                "and deleted it's cached version at \"{}\".",
                _path.generic_string(), _cachePath->generic_string());
        }
        else
        {
            LOG_TRACE("scripts.hotswap", "Lazy unloaded the shared library \"{}\".",
                _path.generic_string());
        }
    }

private:
    fs::path const _path;
    std::optional<fs::path> const _cachePath;
};

typedef std::unique_ptr<typename std::remove_pointer<HandleType>::type, SharedLibraryUnloader> HandleHolder;

typedef char const* (*GetScriptModuleRevisionHashType)();
typedef void (*AddScriptsType)();
typedef char const* (*GetScriptModuleType)();
typedef char const* (*GetBuildDirectiveType)();

class ScriptModule
    : public ModuleReference
{
public:
    ScriptModule(HandleHolder handle, GetScriptModuleRevisionHashType getScriptModuleRevisionHash,
        AddScriptsType addScripts, GetScriptModuleType getScriptModule,
        GetBuildDirectiveType getBuildDirective, fs::path const& path)
        : _handle(std::forward<HandleHolder>(handle)), _getScriptModuleRevisionHash(getScriptModuleRevisionHash),
          _addScripts(addScripts), _getScriptModule(getScriptModule),
          _getBuildDirective(getBuildDirective), _path(path) { }

    ScriptModule(ScriptModule const&) = delete;
    ScriptModule(ScriptModule&& right) = delete;

    ScriptModule& operator=(ScriptModule const&) = delete;
    ScriptModule& operator=(ScriptModule&& right) = delete;

    static std::optional<std::shared_ptr<ScriptModule>>
        CreateFromPath(fs::path const& path, std::optional<fs::path> cachePath);

    static void ScheduleDelayedDelete(ScriptModule* module);

    char const* GetScriptModuleRevisionHash() const override
    {
        return _getScriptModuleRevisionHash();
    }

    void AddScripts() const
    {
        return _addScripts();
    }

    char const* GetScriptModule() const override
    {
        return _getScriptModule();
    }

    char const* GetBuildDirective() const
    {
        return _getBuildDirective();
    }

    fs::path const& GetModulePath() const override
    {
        return _path;
    }

private:
    HandleHolder _handle;

    GetScriptModuleRevisionHashType _getScriptModuleRevisionHash;
    AddScriptsType _addScripts;
    GetScriptModuleType _getScriptModule;
    GetBuildDirectiveType _getBuildDirective;

    fs::path _path;
};

template<typename Fn>
static bool GetFunctionFromSharedLibrary(HandleType handle, std::string const& name, Fn& fn)
{
#if AC_PLATFORM == AC_PLATFORM_WINDOWS
    fn = reinterpret_cast<Fn>(GetProcAddress(handle, name.c_str()));
#else // Posix
    fn = reinterpret_cast<Fn>(dlsym(handle, name.c_str()));
#endif
    return fn != nullptr;
}

// Load a shared library from the given path.
std::optional<std::shared_ptr<ScriptModule>>
    ScriptModule::CreateFromPath(fs::path const& path, std::optional<fs::path> cachePath)
{
    auto const loadPath = [&]() -> fs::path
    {
        if (cachePath)
            return *cachePath;

        return path;
    }();

#if AC_PLATFORM == AC_PLATFORM_WINDOWS
    HandleType handle = LoadLibrary(loadPath.generic_string().c_str());
#else // Posix
    HandleType handle = dlopen(loadPath.generic_string().c_str(), RTLD_LAZY);
#endif

    if (!handle)
    {
        if (cachePath)
        {
            LOG_ERROR("scripts.hotswap", "Could not dynamic load the shared library \"{}\" "
                "(the library is cached at \"{}\").",
                path.generic_string(), cachePath->generic_string());
        }
        else
        {
            LOG_ERROR("scripts.hotswap", "Could not dynamic load the shared library \"{}\".",
                path.generic_string());
        }

        return std::nullopt;
    }

    // Use RAII to release the library on failure.
    HandleHolder holder(handle, SharedLibraryUnloader(path, std::move(cachePath)));

    GetScriptModuleRevisionHashType getScriptModuleRevisionHash;
    AddScriptsType addScripts;
    GetScriptModuleType getScriptModule;
    GetBuildDirectiveType getBuildDirective;

    if (GetFunctionFromSharedLibrary(handle, "GetScriptModuleRevisionHash", getScriptModuleRevisionHash) &&
        GetFunctionFromSharedLibrary(handle, "AddScripts", addScripts) &&
        GetFunctionFromSharedLibrary(handle, "GetScriptModule", getScriptModule) &&
        GetFunctionFromSharedLibrary(handle, "GetBuildDirective", getBuildDirective))
    {
        auto module = new ScriptModule(std::move(holder), getScriptModuleRevisionHash,
            addScripts, getScriptModule, getBuildDirective, path);

        // Unload the module at the next update tick as soon as all references are removed
        return std::shared_ptr<ScriptModule>(module, ScheduleDelayedDelete);
    }

    LOG_ERROR("scripts.hotswap", "Could not extract all required functions from the shared library \"{}\"!",
        path.generic_string());

    return std::nullopt;
}

// Accepts the libraries of the in-tree script modules (scripts_NAME) and of the
// out-of-tree modules (mod_NAME). The part after the prefix may contain dashes,
// because a module directory is named mod-something and its library keeps that
// name.
static bool HasValidScriptModuleName(std::string const& name)
{
    static std::regex const regex(Acore::StringFormat(
        "^{}([sS]cripts_|mod_)[a-zA-Z0-9_-]+\\.{}$",
        GetSharedLibraryPrefix(),
        GetSharedLibraryExtension()));

    return std::regex_match(name, regex);
}

/// Returns false when this platform or this configuration must not rebuild script
/// modules, which is the case on macOS and when the recompiler is disabled by config.
static bool IsRecompilerEnabled()
{
#if AC_PLATFORM == AC_PLATFORM_APPLE
    return false;
#else
    return sWorld->getBoolConfig(CONFIG_HOTSWAP_RECOMPILER_ENABLED);
#endif
}

/// File watcher responsible for watching shared libraries
class LibraryUpdateListener : public efsw::FileWatchListener
{
public:
    LibraryUpdateListener() = default;
    virtual ~LibraryUpdateListener() = default;

    void handleFileAction(efsw::WatchID /*watchid*/, std::string const& dir,
        std::string const& filename, efsw::Action action, std::string oldFilename = "") final override;
};

static LibraryUpdateListener libraryUpdateListener;

/// File watcher responsible for watching source files
class SourceUpdateListener : public efsw::FileWatchListener
{
    fs::path const _path;
    std::string const _scriptModuleName;
    efsw::WatchID const _watcherId;

public:
    explicit SourceUpdateListener(fs::path path, std::string scriptModuleName);

    virtual ~SourceUpdateListener();

    void handleFileAction(efsw::WatchID /*watchid*/, std::string const& dir,
        std::string const& filename, efsw::Action action, std::string oldFilename = "") final override;
};

/// The base implementation of the entry points does nothing: only the reloader
/// that can actually load libraries overrides them.
void ScriptReloadMgr::Initialize()
{
}

void ScriptReloadMgr::LoadSettings()
{
}

void ScriptReloadMgr::Update()
{
}

#if AC_PLATFORM != AC_PLATFORM_APPLE
/// Invokes an asynchronous CMake process with the given arguments. The output of
/// the process is routed into the hotswap log, so nothing blocks the world tick.
static std::shared_ptr<Acore::AsyncProcessResult> InvokeAsyncCMakeCommand(std::vector<std::string> args)
{
    return Acore::StartAsyncProcess(BuiltInConfig::GetCMakeCommand(), std::move(args), "scripts.hotswap");
}

/// Returns true when an attached debugger blocks the rebuild of a script library,
/// which is the case on Windows, where a loaded library cannot be replaced while
/// the process is being debugged.
static bool IsDebuggerBlockingRebuild()
{
#if AC_PLATFORM == AC_PLATFORM_WINDOWS
    return IsDebuggerPresent() != 0;
#else
    return false;
#endif
}
#endif

/// ScriptReloadMgr which is used when dynamic linking is enabled
///
/// This class manages shared library loading/unloading through watching
/// the script module directory. Loaded shared libraries are mirrored
/// into a cache subdirectory to allow lazy unloading as long as
/// the shared library is still used which is useful for scripts
/// which can't be instantly replaced like spells or instances.
/// Several modules which reference different versions can be kept loaded
/// to serve scripts of different versions to entities and spells.
///
/// Also this class invokes rebuilds as soon as the source of loaded
/// scripts change and installs the modules correctly through CMake.
class HotSwapScriptReloadMgr final
    : public ScriptReloadMgr
{
    friend class ScriptReloadMgr;
    friend class SourceUpdateListener;

    /// Reflects a queued change on a source file which is waiting for processing
    enum class ChangeStateRequest : uint8
    {
        CHANGE_REQUEST_ADDED,
        CHANGE_REQUEST_MODIFIED,
        CHANGE_REQUEST_REMOVED
    };

#if AC_PLATFORM != AC_PLATFORM_APPLE
    /// Reflects a running job of an invoked asynchronous external process
    enum class BuildJobType : uint8
    {
        BUILD_JOB_NONE,
        BUILD_JOB_RERUN_CMAKE,
        BUILD_JOB_COMPILE,
        BUILD_JOB_INSTALL
    };

    /// Represents a job which was invoked through a source change of a module
    class BuildJob
    {
    public:
        BuildJob(std::string moduleName, std::string projectName, std::string buildDirective)
            : _moduleName(std::move(moduleName)), _projectName(std::move(projectName)),
              _buildDirective(std::move(buildDirective)), _startTime(getMSTime()),
              _type(BuildJobType::BUILD_JOB_NONE) { }

        bool IsValid() const
        {
            return _type != BuildJobType::BUILD_JOB_NONE;
        }

        std::string const& GetModuleName() const { return _moduleName; }

        std::string const& GetProjectName() const { return _projectName; }

        std::string const& GetBuildDirective() const { return _buildDirective; }

        uint32 GetTimeFromStart() const { return GetMSTimeDiffToNow(_startTime); }

        BuildJobType GetType() const { return _type; }

        std::shared_ptr<Acore::AsyncProcessResult> const& GetProcess() const
        {
            ASSERT(_asyncResult, "Tried to access an empty process handle!");
            return _asyncResult;
        }

        /// Updates the current running job with the given type and async result
        void UpdateCurrentJob(BuildJobType type, std::shared_ptr<Acore::AsyncProcessResult> asyncResult)
        {
            ASSERT(type != BuildJobType::BUILD_JOB_NONE, "None isn't allowed here!");
            ASSERT(asyncResult, "The async result must not be empty!");

            _type = type;
            _asyncResult = std::move(asyncResult);
        }

    private:
        // The script module which is processed
        std::string _moduleName;
        // The CMake project name of the module which is processed
        std::string _projectName;
        // The build directive the module is built with, like "Release" or "Debug"
        std::string _buildDirective;
        // The time the job started
        uint32 _startTime;

        // The type of the current running job
        BuildJobType _type;
        // The async process result of the current running job
        std::shared_ptr<Acore::AsyncProcessResult> _asyncResult;
    };
#endif

    /// Base class for lockfree asynchronous messages to the script reloader
    class ScriptReloaderMessage
    {
    public:
        virtual ~ScriptReloaderMessage() = default;

        /// Invoke this function to run a message thread safe on the reloader
        virtual void operator()(HotSwapScriptReloadMgr* reloader) = 0;
    };

    /// Implementation class which wraps functional types and dispatches
    /// it in the overwritten implementation of the reloader messages.
    template<typename T>
    class ScriptReloaderMessageImplementation
        : public ScriptReloaderMessage
    {
        T _dispatcher;

    public:
        explicit ScriptReloaderMessageImplementation(T dispatcher)
            : _dispatcher(std::move(dispatcher)) { }

        void operator()(HotSwapScriptReloadMgr* reloader) final override
        {
            _dispatcher(reloader);
        }
    };

    /// Uses the given functional type and creates a asynchronous reloader
    /// message on the heap, which requires deletion.
    template<typename T>
    static auto MakeMessage(T&& dispatcher)
        -> ScriptReloaderMessageImplementation<typename std::decay<T>::type>*
    {
        return new ScriptReloaderMessageImplementation<typename std::decay<T>::type>
            (std::forward<T>(dispatcher));
    }

public:
    HotSwapScriptReloadMgr()
        : _libraryWatcher(-1), _uniqueLibraryNameCounter(0), _lastTimeLibraryChanged(0),
          _lastTimeSourcesChanged(0) { }

    virtual ~HotSwapScriptReloadMgr()
    {
        // Delete all messages
        ScriptReloaderMessage* message;
        while (_messages.Dequeue(message))
            delete message;
    }

    /// Returns the absolute path to the script module directory
    static fs::path GetLibraryDirectory()
    {
        // When an absolute path is given in the config use it,
        // otherwise interpret paths relative to the executable.
        fs::path path(sConfigMgr->GetOption<std::string>("HotSwap.ScriptDir", "scripts"));
        if (path.is_absolute())
            return path;

        return AbsolutePath(path, GetDirectoryOfExecutable());
    }

    /// Returns the absolute path to the scripts directory in the source tree.
    static fs::path GetSourceDirectory()
    {
        fs::path dir = BuiltInConfig::GetSourceDirectory();
        dir /= "src";
        dir /= "server";
        dir /= "scripts";
        return dir;
    }

    /// Starts nothing yet: the scripts are registered before the world configuration
    /// is read, so the manager only announces that it is waiting for it.
    void Initialize() final override
    {
        LOG_INFO("scripts.hotswap", "Script hotswapping is waiting for the world configuration.");
    }

    /// Resolves the settings the reloader runs with, loads the libraries that are
    /// installed already and starts the watchers.
    void LoadSettings() final override
    {
        if (!sWorld->getBoolConfig(CONFIG_HOTSWAP_ENABLED))
        {
            LOG_INFO("scripts.hotswap", "Script hotswapping is disabled by HotSwap.Enabled.");
            return;
        }

        if (BuiltInConfig::GetBuildDirectory().find(' ') != std::string::npos)
        {
            LOG_ERROR("scripts.hotswap", "Your build directory path \"{}\" "
                "contains spaces, which isn't allowed for compatibility reasons! "
                "You need to create a build directory which doesn't contain any space character "
                "in it's path!",
                BuiltInConfig::GetBuildDirectory());

            return;
        }

        auto const libraryDirectory = GetLibraryDirectory();
        {
            std::error_code code;
            if (!fs::exists(libraryDirectory, code) || !fs::is_directory(libraryDirectory, code))
            {
                LOG_ERROR("scripts.hotswap", "Library directory \"{}\" doesn't exist!",
                    libraryDirectory.generic_string());
                return;
            }
        }

        _temporaryCachePath = CalculateTemporaryCachePath();

        // A cache directory of an earlier run holds versions nobody references any
        // more, so it is discarded instead of being reused.
        {
            std::error_code code;
            fs::remove_all(_temporaryCachePath, code);
            code.clear();
            if (!fs::create_directories(_temporaryCachePath, code) && code)
            {
                LOG_ERROR("scripts.hotswap", "Couldn't create the cache directory at \"{}\" ({}).",
                    _temporaryCachePath.generic_string(), code.message());

                return;
            }
        }

        LOG_INFO("scripts.hotswap", "Script hotswapping is enabled: script directory \"{}\", "
            "cache directory \"{}\", recompiler {}.",
            libraryDirectory.generic_string(), _temporaryCachePath.generic_string(),
            IsRecompilerEnabled() ? "enabled" : "disabled");

        // Say why a source change is ignored instead of leaving the operator guessing
        if (!IsRecompilerEnabled())
        {
#if AC_PLATFORM == AC_PLATFORM_APPLE
            LOG_INFO("scripts.hotswap", ">> The script recompiler is not supported on macOS, so source "
                "changes are ignored. Prebuilt script libraries are still loaded and reloaded.");
#else
            LOG_INFO("scripts.hotswap", ">> The script recompiler is disabled by HotSwap.EnableReCompiler, "
                "so source changes are ignored. Prebuilt script libraries are still loaded and reloaded.");
#endif
        }

        // Point the build at the tree this server runs from, so a rebuild
        // installs next to it instead of into a prefix that is gone.
        if (sWorld->getBoolConfig(CONFIG_HOTSWAP_PREFIX_CORRECTION_ENABLED))
            DoCMakePrefixCorrectionIfNeeded();

        _settingsLoaded = true;

        InitializeDefaultLibraries();
        InitializeFileWatchers();

        // The libraries loaded above are the boot set, so they are committed as a
        // start-up change set: every live object already runs the code they select,
        // and the next real change must only rebuild what that change touched.
        sScriptMgr->SwapScriptContext(true);
    }

    /// Needs to be called periodically from the worldserver loop
    /// to invoke queued actions like module loading/unloading.
    /// This method should be invoked from a thread safe point to
    /// prevent misbehavior.
    void Update() final override
    {
        // LoadSettings() has not run yet, so there is nothing to watch or to load.
        if (!_settingsLoaded)
            return;

        // Consume all messages
        ScriptReloaderMessage* message;
        while (_messages.Dequeue(message))
        {
            (*message)(this);
            delete message;
        }

        // A commit the world refused while database scripts were running is applied
        // as soon as they are done, on the next tick.
        if (sScriptMgr->IsSwapPending())
            sScriptMgr->SwapScriptContext();

#if AC_PLATFORM != AC_PLATFORM_APPLE
        DispatchRunningBuildJobs();
#endif
        DispatchModuleChanges();
    }

    /// Unloads the manager and cancels all runnings jobs immediately
    void Unload() final override
    {
        if (_libraryWatcher >= 0)
        {
            _fileWatcher.removeWatch(_libraryWatcher);
            _libraryWatcher = -1;
        }

#if AC_PLATFORM != AC_PLATFORM_APPLE
        // A running build is cancelled, so no library is installed while the
        // server tears down
        if (_buildJob)
        {
            LOG_INFO("scripts.hotswap", "Cancelling the build of module \"{}\" (project \"{}\", build "
                "directive \"{}\") because the server shuts down.",
                _buildJob->GetModuleName(), _buildJob->GetProjectName(), _buildJob->GetBuildDirective());

            _buildJob->GetProcess()->Terminate();
            _buildJob.reset();
        }
#endif

        // Release all strong references to script modules
        // to trigger unload actions as early as possible,
        // otherwise the worldserver will crash on exit.
        _runningScriptModules.clear();
        _runningScriptModuleNames.clear();
        _librariesChanged.clear();
        _sourcesChanged.clear();
    }

    /// Queue's a thread safe message to the reloader which is executed on
    /// the next world server update tick.
    template<typename T>
    void QueueMessage(T&& message)
    {
        _messages.Enqueue(MakeMessage(std::forward<T>(message)));
    }

    /// Queues an action which marks the given shared library as changed
    /// which will add, unload or reload it at the next world update tick.
    void QueueSharedLibraryChanged(fs::path const& path)
    {
        _lastTimeLibraryChanged = getMSTime();
        _librariesChanged.insert(path);
    }

    /// Queues a notification that a source file was added
    void QueueAddSourceFile(std::string const& moduleName, fs::path const& path)
    {
        UpdateSourceChangeRequest(moduleName, path, ChangeStateRequest::CHANGE_REQUEST_ADDED);
    }

    /// Queues a notification that a source file was modified
    void QueueModifySourceFile(std::string const& moduleName, fs::path const& path)
    {
        UpdateSourceChangeRequest(moduleName, path, ChangeStateRequest::CHANGE_REQUEST_MODIFIED);
    }

    /// Queues a notification that a source file was removed
    void QueueRemoveSourceFile(std::string const& moduleName, fs::path const& path)
    {
        UpdateSourceChangeRequest(moduleName, path, ChangeStateRequest::CHANGE_REQUEST_REMOVED);
    }

private:
    /// Points the CMAKE_INSTALL_PREFIX of the build this server was linked
    /// against at the tree the server currently runs from, since that is where
    /// a rebuild has to install to become visible to the next reload.
    void DoCMakePrefixCorrectionIfNeeded()
    {
        std::string const buildDirectory = BuiltInConfig::GetBuildDirectory();

        LOG_INFO("scripts.hotswap", "Checking the CMAKE_INSTALL_PREFIX of the build in \"{}\"...",
            buildDirectory);

        auto const cmakeCachePath = AbsolutePath("CMakeCache.txt", buildDirectory);

        {
            std::error_code code;
            if (!fs::exists(cmakeCachePath, code))
            {
                LOG_ERROR("scripts.hotswap", ">> CMake cache \"{}\" doesn't exist, set the "
                    "\"BuildDirectory\" option in your worldserver.conf to point to your build directory!",
                    cmakeCachePath.generic_string());

                return;
            }
        }

        std::string cmakeCacheContent;
        {
            std::ifstream in(cmakeCachePath.generic_string());
            if (!in.is_open())
            {
                LOG_ERROR("scripts.hotswap", ">> Failed to read the CMake cache at \"{}\"!",
                    cmakeCachePath.generic_string());

                return;
            }

            std::ostringstream ss;
            ss << in.rdbuf();
            cmakeCacheContent = ss.str();
        }

        // The worldserver is installed into ${CMAKE_INSTALL_PREFIX}/bin everywhere
        // but on Windows, where it is installed directly into the prefix.
        fs::path installPrefix = fs::current_path();
#if AC_PLATFORM != AC_PLATFORM_WINDOWS
        installPrefix = installPrefix.parent_path();
#endif

        static std::string const prefixKey = "CMAKE_INSTALL_PREFIX:PATH=";

        // Extract the value of CMAKE_INSTALL_PREFIX
        auto begin = cmakeCacheContent.find(prefixKey);
        if (begin != std::string::npos)
        {
            begin += prefixKey.length();
            auto const end = cmakeCacheContent.find('\n', begin);
            if (end != std::string::npos)
            {
                fs::path const value = cmakeCacheContent.substr(begin, end - begin);

                if (value == installPrefix)
                {
                    LOG_INFO("scripts.hotswap", ">> CMAKE_INSTALL_PREFIX is equal to the current "
                        "path of execution.");

                    return;
                }

                // A server started from inside its own build tree must not rewrite
                // the prefix that tree was configured with.
                if (IsPathWithin(value, buildDirectory) || IsPathWithin(installPrefix, buildDirectory))
                {
                    LOG_DEBUG("scripts.hotswap", ">> CMAKE_INSTALL_PREFIX is not corrected, the server "
                        "runs from inside the build directory \"{}\".", buildDirectory);

                    return;
                }

                LOG_INFO("scripts.hotswap", ">> Found outdated CMAKE_INSTALL_PREFIX (\"{}\"), "
                    "worldserver is currently installed at \"{}\".",
                    value.generic_string(), installPrefix.generic_string());
            }
        }

        LOG_INFO("scripts.hotswap", "Invoking the CMake cache correction...");

        int const result = Acore::StartProcess(BuiltInConfig::GetCMakeCommand(), {
            "-DCMAKE_INSTALL_PREFIX:PATH=" + installPrefix.generic_string(),
            buildDirectory
        }, "scripts.hotswap");

        if (result)
        {
            LOG_ERROR("scripts.hotswap", ">> Failed to update the CMAKE_INSTALL_PREFIX, the build "
                "command exited with {}! This could lead to unexpected behaviour!", result);
        }
        else
        {
            LOG_INFO("scripts.hotswap", ">> Successfully corrected the CMAKE_INSTALL_PREFIX to point "
                "at \"{}\".", installPrefix.generic_string());
        }
    }

    // Loads all shared libraries which are contained in the
    // scripts directory on startup.
    void InitializeDefaultLibraries()
    {
        fs::path const libraryDirectory(GetLibraryDirectory());
        fs::directory_iterator const dirEnd;

        uint32 count = 0;

        std::error_code code;
        for (fs::directory_iterator dirItr(libraryDirectory, code); !code && dirItr != dirEnd; dirItr.increment(code))
        {
            if (!fs::is_regular_file(dirItr->path(), code) ||
                !HasValidScriptModuleName(dirItr->path().filename().generic_string()))
                continue;

            LOG_INFO("scripts.hotswap", "Loading script module \"{}\"...",
                dirItr->path().filename().generic_string());

            // Don't swap the script context to do bulk loading
            ProcessLoadScriptModule(dirItr->path(), false);
            ++count;
        }

        LOG_INFO("scripts.hotswap", ">> Loaded {} script module(s).", count);
    }

    // Initialize all enabled file watchers.
    // Needs to be called after InitializeDefaultLibraries()!
    void InitializeFileWatchers()
    {
        auto const libraryDirectory = GetLibraryDirectory();
        _libraryWatcher = _fileWatcher.addWatch(libraryDirectory.generic_string(), &libraryUpdateListener, false);
        if (_libraryWatcher >= 0)
        {
            LOG_INFO("scripts.hotswap", ">> Library reloader is listening on \"{}\".",
                libraryDirectory.generic_string());
        }
        else
        {
            LOG_ERROR("scripts.hotswap", "Failed to initialize the library reloader on \"{}\".",
                libraryDirectory.generic_string());
        }

        _fileWatcher.watch();
    }

    static fs::path CalculateTemporaryCachePath()
    {
        // A branch name can contain a path separator, which a directory name cannot.
        std::string branch(GitRevision::GetBranch());
        std::replace(branch.begin(), branch.end(), '/', '_');

        auto path = fs::temp_directory_path();
        path /= Acore::StringFormat("ac_script_cache_{}_{}",
            branch,
            ByteArrayToHexStr(Acore::Crypto::SHA1::GetDigestOf(sConfigMgr->GetFilename())));

        return path;
    }

    fs::path GenerateUniquePathForLibraryInCache(fs::path path)
    {
        ASSERT(!_temporaryCachePath.empty(),
            "The temporary cache path wasn't set!");

        // Create the cache path and increment the library counter to use an unique name for each library
        auto cachePath = _temporaryCachePath;
        cachePath /= Acore::StringFormat("{}.{}{}",
            path.stem().generic_string(),
            _uniqueLibraryNameCounter++,
            path.extension().generic_string());

        return cachePath;
    }

    /// Updates the current state of the given source path
    void UpdateSourceChangeRequest(std::string const& moduleName, fs::path const& path, ChangeStateRequest state)
    {
        _lastTimeSourcesChanged = getMSTime();

        // Write when there is no module with the given name known
        auto moduleItr = _sourcesChanged.find(moduleName);

        // When the file was modified it's enough to mark the module as
        // dirty by initializing the associated map.
        if (moduleItr == _sourcesChanged.end())
            moduleItr = _sourcesChanged.insert(std::make_pair(
                moduleName, decltype(_sourcesChanged)::mapped_type{})).first;

        // Leave when the file was just modified as explained above
        if (state == ChangeStateRequest::CHANGE_REQUEST_MODIFIED)
            return;

        // Insert when the given path isn't existent
        auto const itr = moduleItr->second.find(path);
        if (itr == moduleItr->second.end())
        {
            moduleItr->second.insert(std::make_pair(path, state));
            return;
        }

        ASSERT((itr->second == ChangeStateRequest::CHANGE_REQUEST_ADDED)
            || (itr->second == ChangeStateRequest::CHANGE_REQUEST_REMOVED),
            "Stored value is invalid!");

        ASSERT((state == ChangeStateRequest::CHANGE_REQUEST_ADDED)
            || (state == ChangeStateRequest::CHANGE_REQUEST_REMOVED),
            "The given state is invalid!");

        ASSERT(state != itr->second,
            "Tried to apply a state which is stored already!");

        moduleItr->second.erase(itr);
    }

    /// Called periodically on the worldserver tick to process all
    /// load/unload/reload requests of shared libraries.
    void DispatchModuleChanges()
    {
        // When there are no libraries to change return
        if (_librariesChanged.empty())
            return;

        // Wait some time after changes to catch bulk changes
        if (GetMSTimeDiffToNow(_lastTimeLibraryChanged) < 500)
            return;

        for (auto const& path : _librariesChanged)
        {
            bool const isRunning =
                _runningScriptModuleNames.find(path) != _runningScriptModuleNames.end();

            std::error_code code;
            bool const exists = fs::exists(path, code);

            if (isRunning)
            {
                if (exists)
                    ProcessReloadScriptModule(path);
                else
                    ProcessUnloadScriptModule(path);
            }
            else if (exists)
                ProcessLoadScriptModule(path);
        }

        _librariesChanged.clear();
    }

    void ProcessLoadScriptModule(fs::path const& path, bool swapContext = true)
    {
        ASSERT(_runningScriptModuleNames.find(path) == _runningScriptModuleNames.end(),
            "Can't load a module which is running already!");

        // Copy the shared library into a cache
        auto const cachePath = GenerateUniquePathForLibraryInCache(path);

        {
            std::error_code code;
            fs::copy_file(path, cachePath, fs::copy_options::overwrite_existing, code);
            if (code)
            {
                LOG_FATAL("scripts.hotswap", ">> Failed to create cache entry for module "
                    "\"{}\" at \"{}\" with reason (\"{}\")!",
                    path.filename().generic_string(), cachePath.generic_string(),
                    code.message());

                // Find a better solution for this but it's much better
                // to start the core without scripts
                std::this_thread::sleep_for(std::chrono::seconds(5));
                ABORT();
                return;
            }

            LOG_TRACE("scripts.hotswap", ">> Copied the shared library \"{}\" to \"{}\" for caching.",
                path.filename().generic_string(), cachePath.generic_string());
        }

        auto module = ScriptModule::CreateFromPath(path, cachePath);
        if (!module)
        {
            LOG_FATAL("scripts.hotswap", ">> Failed to load script module \"{}\"!",
                path.filename().generic_string());

            // Find a better solution for this but it's much better
            // to start the core without scripts
            std::this_thread::sleep_for(std::chrono::seconds(5));
            ABORT();
            return;
        }

        // Limit the git revision hash to 7 characters.
        std::string moduleRevision((*module)->GetScriptModuleRevisionHash());
        if (moduleRevision.size() >= 7)
            moduleRevision = moduleRevision.substr(0, 7);

        std::string const moduleName = (*module)->GetScriptModule();
        LOG_INFO("scripts.hotswap", ">> Loaded script module \"{}\" (\"{}\" - {}).",
            path.filename().generic_string(), moduleName, moduleRevision);

        if (moduleRevision.empty())
        {
            LOG_WARN("scripts.hotswap", ">> Script module \"{}\" has an empty revision hash!",
                path.filename().generic_string());
        }
        else
        {
            // A build without git information cannot be compared with anything.
            std::string myRevisionHash(GitRevision::GetHash());
            if (myRevisionHash.empty() || myRevisionHash == "unknown")
            {
                LOG_DEBUG("scripts.hotswap", ">> Script module \"{}\" is compared against a core "
                    "built without a revision hash.", path.filename().generic_string());
            }
            else
            {
                // Trim the revision hash
                std::size_t const trim = std::min(moduleRevision.size(), myRevisionHash.size());
                myRevisionHash = myRevisionHash.substr(0, trim);
                std::string const comparedRevision = moduleRevision.substr(0, trim);

                if (myRevisionHash != comparedRevision)
                {
                    LOG_WARN("scripts.hotswap", ">> Script module \"{}\" has a different revision hash! "
                        "Binary incompatibility could lead to unknown behaviour!",
                        path.filename().generic_string());
                }
            }
        }

        {
            auto const itr = _runningScriptModules.find(moduleName);
            if (itr != _runningScriptModules.end())
            {
                LOG_ERROR("scripts.hotswap", ">> Attempt to load a module twice \"{}\" (loaded module is at \"{}\")!",
                    path.generic_string(), itr->second.first->GetModulePath().generic_string());

                return;
            }
        }

        // Create the source listener
        auto listener = std::make_unique<SourceUpdateListener>(GetSourceDirectory() / moduleName, moduleName);

        // Store the module
        _knownModulesBuildDirectives.insert(std::make_pair(moduleName, (*module)->GetBuildDirective()));
        _runningScriptModules.insert(std::make_pair(moduleName,
            std::make_pair(*module, std::move(listener))));
        _runningScriptModuleNames.insert(std::make_pair(path, moduleName));

        // Process the script loading after the module was registered correctly.
        sScriptMgr->SetScriptContext(moduleName);
        (*module)->AddScripts();
        LOG_TRACE("scripts.hotswap", ">> Registered all scripts of module \"{}\".", moduleName);

        if (swapContext)
            sScriptMgr->SwapScriptContext();
    }

    void ProcessReloadScriptModule(fs::path const& path)
    {
        // One commit for both halves, so the swap is atomic.
        ProcessUnloadScriptModule(path, false);
        ProcessLoadScriptModule(path);
    }

    void ProcessUnloadScriptModule(fs::path const& path, bool finish = true)
    {
        auto const itr = _runningScriptModuleNames.find(path);

        ASSERT(itr != _runningScriptModuleNames.end(),
            "Can't unload a module which isn't running!");

        // Unload the script context
        sScriptMgr->ReleaseScriptContext(itr->second);

        if (finish)
            sScriptMgr->SwapScriptContext();

        LOG_INFO("scripts.hotswap", "Released script module \"{}\" (\"{}\")...",
            path.filename().generic_string(), itr->second);

        // Unload the script module
        auto const ref = _runningScriptModules.find(itr->second);
        ASSERT(ref != _runningScriptModules.end(),
            "Expected the script reference to be present!");

        // Yield a message when there are other owning references to
        // the module which prevents it from unloading.
        // The module will be unloaded once all scripts provided from the module
        // are destroyed.
        if (ref->second.first.use_count() != 1)
        {
            LOG_INFO("scripts.hotswap",
                "Script module \"{}\" is still used by {} spell, aura or instance script(s). "
                "Will lazy unload the module once all scripts stopped using it, "
                "to use the latest version of an edited script unbind yourself from "
                "the instance or re-cast the spell.",
                ref->second.first->GetScriptModule(), ref->second.first.use_count() - 1);
        }

        // Remove the owning reference from the reloader
        _runningScriptModules.erase(ref);
        _runningScriptModuleNames.erase(itr);
    }

#if AC_PLATFORM != AC_PLATFORM_APPLE
    /// Returns the name of the CMake project which builds the library of the given
    /// module: scripts_<module> for a module of the source tree and mod_<module> for
    /// an out-of-tree module, both lowercased to match the CMake helpers
    /// GetProjectNameOfScriptModule and GetProjectNameOfModuleName.
    std::string CalculateProjectNameOfModule(std::string const& moduleName) const
    {
        // The kind is taken from the file name of the library, never from the name
        // the library reports, since that one is the plain directory name.
        bool isOutOfTreeModule = false;
        for (auto const& entry : _runningScriptModuleNames)
        {
            if (entry.second != moduleName)
                continue;

            std::string const fileName = entry.first.filename().generic_string();
            std::string const prefix(GetSharedLibraryPrefix());
            isOutOfTreeModule = fileName.compare(prefix.size(), 4, "mod_") == 0;
            break;
        }

        std::string projectName = (isOutOfTreeModule ? "mod_" : "scripts_") + moduleName;
        std::transform(projectName.begin(), projectName.end(), projectName.begin(), charToLower);

        return projectName;
    }

    /// Returns the build directive the given module is built with: the one which is
    /// configured for all modules when it is set, the one the module was built with
    /// otherwise and the one of the core as the last resort.
    std::string CalculateBuildDirectiveOfModule(std::string const& moduleName) const
    {
        std::string directive = sConfigMgr->GetOption<std::string>("HotSwap.ReCompilerBuildType", "");
        if (!directive.empty())
            return directive;

        auto const itr = _knownModulesBuildDirectives.find(moduleName);
        if (itr != _knownModulesBuildDirectives.end())
            return itr->second;

        // Fall back to the directive the core itself was built with
        return AC_BUILD_TYPE;
    }

    /// Called periodically on the worldserver tick to process all recompile
    /// requests. This method invokes one build or install job at the time.
    void DispatchRunningBuildJobs()
    {
        if (_buildJob)
        {
            // A source of the module which is being built changed, so the result of
            // the running job is outdated: mark it and let the job be discarded when
            // it finishes, terminating its process from a file event thread crashes.
            if (sWorld->getBoolConfig(CONFIG_HOTSWAP_EARLY_TERMINATION_ENABLED) && !_terminateEarly &&
                _sourcesChanged.find(_buildJob->GetModuleName()) != _sourcesChanged.end())
            {
                LOG_INFO("scripts.hotswap", "The sources of module \"{}\" (project \"{}\") changed while "
                    "it was built, the result of the running build is discarded when it finishes.",
                    _buildJob->GetModuleName(), _buildJob->GetProjectName());

                _terminateEarly = true;
                return;
            }

            // Wait for the running job to finish, evaluate it as soon as it did
            if (_buildJob->GetProcess()->GetFutureResult().wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready)
                ProcessReadyBuildJob();
            else
                return; // The running job hasn't finished yet

            // The finished job started the next one of the same module
            if (_buildJob)
                return;
        }

        // Avoid burst updates through waiting some time after the last change
        if (_lastTimeSourcesChanged != 0 && GetMSTimeDiffToNow(_lastTimeSourcesChanged) < 500)
            return;

        // Nothing to rebuild
        if (_sourcesChanged.empty())
            return;

        // Wait until an attached debugger is detached, it blocks the rebuild
        if (IsDebuggerBlockingRebuild())
        {
            if (_lastTimeUserInformed == 0 || GetMSTimeDiffToNow(_lastTimeUserInformed) > 7500)
            {
                _lastTimeUserInformed = getMSTime();

                LOG_INFO("scripts.hotswap", "Your attached debugger is blocking the automatic script "
                    "rebuild, please detach it!");
            }

            return;
        }

        // Take the next changed module out of the list
        auto const itr = _sourcesChanged.begin();

        std::string const moduleName = itr->first;
        bool const rebuildBuildFiles = !itr->second.empty();

        for (auto const& entry : itr->second)
        {
            LOG_TRACE("scripts.hotswap", "Source file \"{}\" of module \"{}\" was {}.",
                entry.first.generic_string(), moduleName,
                (entry.second == ChangeStateRequest::CHANGE_REQUEST_ADDED ? "added" : "removed"));
        }

        _sourcesChanged.erase(itr);

        // A re-run of CMake picks up the added and removed files of every module,
        // so the records of the other modules are consumed by it as well.
        if (rebuildBuildFiles)
            for (auto& entry : _sourcesChanged)
                entry.second.clear();

        ASSERT(!moduleName.empty(), "The current module name is invalid!");

        std::string const projectName = CalculateProjectNameOfModule(moduleName);
        std::string const buildDirective = CalculateBuildDirectiveOfModule(moduleName);

        LOG_INFO("scripts.hotswap", "Recompiling script module \"{}\" (project \"{}\", build directive \"{}\")...",
            moduleName, projectName, buildDirective);

        _buildJob.emplace(moduleName, projectName, buildDirective);

        // Rerun CMake when the build files have to be recreated for added or
        // removed sources
        if (rebuildBuildFiles && sWorld->getBoolConfig(CONFIG_HOTSWAP_BUILD_FILE_RECREATION_ENABLED))
            DoRerunCMake();
        else
            DoCompileCurrentProcessedModule();
    }

    /// Evaluates the result of the finished build job and continues with the next
    /// step of it, which is the installation of a successfully built module.
    void ProcessReadyBuildJob()
    {
        ASSERT(_buildJob->IsValid(), "Invalid build job!");

        // Consume the result of the finished process
        int const exitStatus = _buildJob->GetProcess()->GetFutureResult().get();

        if (_terminateEarly)
        {
            LOG_INFO("scripts.hotswap", "Discarded the build of module \"{}\" (project \"{}\", build "
                "directive \"{}\", exit status {}) because its sources changed while it ran.",
                _buildJob->GetModuleName(), _buildJob->GetProjectName(), _buildJob->GetBuildDirective(),
                exitStatus);

            _buildJob.reset();
            _terminateEarly = false;
            return;
        }

        switch (_buildJob->GetType())
        {
            case BuildJobType::BUILD_JOB_RERUN_CMAKE:
            {
                if (!exitStatus)
                {
                    LOG_INFO("scripts.hotswap", ">> Successfully updated the build files of module \"{}\" "
                        "(project \"{}\").", _buildJob->GetModuleName(), _buildJob->GetProjectName());
                }
                else
                {
                    LOG_ERROR("scripts.hotswap", ">> Updating the build files of module \"{}\" (project "
                        "\"{}\", build directive \"{}\") failed with exit status {}! Recently added sources "
                        "may be missing from the next build, rerun CMake manually.",
                        _buildJob->GetModuleName(), _buildJob->GetProjectName(),
                        _buildJob->GetBuildDirective(), exitStatus);
                }

                // Continue with building the changed sources
                DoCompileCurrentProcessedModule();
                return;
            }
            case BuildJobType::BUILD_JOB_COMPILE:
            {
                if (!exitStatus)
                {
                    if (sWorld->getBoolConfig(CONFIG_HOTSWAP_INSTALL_ENABLED))
                    {
                        LOG_INFO("scripts.hotswap", ">> Successfully built module \"{}\" (project \"{}\", "
                            "build directive \"{}\"), continue with installing...",
                            _buildJob->GetModuleName(), _buildJob->GetProjectName(),
                            _buildJob->GetBuildDirective());

                        DoInstallCurrentProcessedModule();
                        return;
                    }

                    LOG_INFO("scripts.hotswap", ">> Successfully built module \"{}\" (project \"{}\", "
                        "build directive \"{}\"), skipped the installation because it is disabled by "
                        "HotSwap.EnableInstall.", _buildJob->GetModuleName(), _buildJob->GetProjectName(),
                        _buildJob->GetBuildDirective());
                }
                else
                {
                    LOG_ERROR("scripts.hotswap", ">> The build of module \"{}\" (project \"{}\", build "
                        "directive \"{}\") failed with exit status {}! See the log for details.",
                        _buildJob->GetModuleName(), _buildJob->GetProjectName(),
                        _buildJob->GetBuildDirective(), exitStatus);
                }
                break;
            }
            case BuildJobType::BUILD_JOB_INSTALL:
            {
                if (!exitStatus)
                {
                    LOG_INFO("scripts.hotswap", ">> Successfully installed module \"{}\" (project \"{}\", "
                        "build directive \"{}\") in {}s.", _buildJob->GetModuleName(),
                        _buildJob->GetProjectName(), _buildJob->GetBuildDirective(),
                        _buildJob->GetTimeFromStart() / IN_MILLISECONDS);
                }
                else
                {
                    LOG_ERROR("scripts.hotswap", ">> The installation of module \"{}\" (project \"{}\", "
                        "build directive \"{}\") failed with exit status {}! See the log for details.",
                        _buildJob->GetModuleName(), _buildJob->GetProjectName(),
                        _buildJob->GetBuildDirective(), exitStatus);
                }
                break;
            }
            default:
                break;
        }

        _buildJob.reset();
    }

    /// Reruns CMake over the build directory to recreate the build files
    void DoRerunCMake()
    {
        ASSERT(_buildJob, "There isn't any active build job!");

        LOG_INFO("scripts.hotswap", "Rerunning CMake for module \"{}\" (project \"{}\") because sources "
            "were added or removed...", _buildJob->GetModuleName(), _buildJob->GetProjectName());

        _buildJob->UpdateCurrentJob(BuildJobType::BUILD_JOB_RERUN_CMAKE,
            InvokeAsyncCMakeCommand({ BuiltInConfig::GetBuildDirectory() }));
    }

    /// Invokes a build of the module of the current job
    void DoCompileCurrentProcessedModule()
    {
        ASSERT(_buildJob, "There isn't any active build job!");

        LOG_INFO("scripts.hotswap", "Starting the build of module \"{}\" (project \"{}\", build directive "
            "\"{}\")...", _buildJob->GetModuleName(), _buildJob->GetProjectName(),
            _buildJob->GetBuildDirective());

        _buildJob->UpdateCurrentJob(BuildJobType::BUILD_JOB_COMPILE,
            InvokeAsyncCMakeCommand({ "--build", BuiltInConfig::GetBuildDirectory(),
                "--target", _buildJob->GetProjectName(),
                "--config", _buildJob->GetBuildDirective() }));
    }

    /// Invokes an installation of the module of the current job
    void DoInstallCurrentProcessedModule()
    {
        ASSERT(_buildJob, "There isn't any active build job!");

        LOG_INFO("scripts.hotswap", "Starting the installation of module \"{}\" (project \"{}\", build "
            "directive \"{}\")...", _buildJob->GetModuleName(), _buildJob->GetProjectName(),
            _buildJob->GetBuildDirective());

        _buildJob->UpdateCurrentJob(BuildJobType::BUILD_JOB_INSTALL,
            InvokeAsyncCMakeCommand({ "-DCOMPONENT=" + _buildJob->GetProjectName(),
                "-DBUILD_TYPE=" + _buildJob->GetBuildDirective(),
                "-P", AbsolutePath("cmake_install.cmake",
                    BuiltInConfig::GetBuildDirectory()).generic_string() }));
    }
#endif

    // File watcher instance and watcher ID's
    efsw::FileWatcher _fileWatcher;
    efsw::WatchID _libraryWatcher;

    // Unique library name counter which is used to
    // generate unique names for every shared library version.
    uint32 _uniqueLibraryNameCounter;

    // Queue which is used for thread safe message processing
    MPSCQueue<ScriptReloaderMessage> _messages;

    // True as soon as the settings were read and the libraries of the install
    // directory were loaded.
    bool _settingsLoaded{ false };

    // Change requests to load or unload shared libraries
    std::unordered_set<fs::path /*path*/> _librariesChanged;
    // The timestamp which indicates the last time a library was changed
    uint32 _lastTimeLibraryChanged;

    // Contains all running script modules
    // The associated shared libraries are unloaded immediately
    // on loosing ownership through RAII.
    std::unordered_map<std::string /*module name*/,
        std::pair<std::shared_ptr<ScriptModule>, std::unique_ptr<SourceUpdateListener>>> _runningScriptModules;
    // Container which maps the path of a shared library to it's module name
    std::unordered_map<fs::path, std::string /*module name*/> _runningScriptModuleNames;
    // Container which maps the module name to it's last known build directive
    std::unordered_map<std::string /*module name*/, std::string /*build directive*/> _knownModulesBuildDirectives;

    // Modules which were changed and are queued for recompilation
    std::unordered_map<std::string /*module*/,
        std::unordered_map<fs::path /*path*/, ChangeStateRequest /*state*/>> _sourcesChanged;
    // Tracks the time since the last module has changed to avoid burst updates
    uint32 _lastTimeSourcesChanged;

#if AC_PLATFORM != AC_PLATFORM_APPLE
    // Represents the build job which is in progress, there is only one at a time
    std::optional<BuildJob> _buildJob;
    // Tracks the last timestamp the user was informed about a repeating event
    uint32 _lastTimeUserInformed{ 0 };
    // Is true when the result of the running build job is discarded as soon as it
    // finishes, because a source of its module changed while it was built
    bool _terminateEarly{ false };
#endif

    // The path to the ac_script_cache temporary cache
    fs::path _temporaryCachePath;
};

class ScriptModuleDeleteMessage
{
public:
    explicit ScriptModuleDeleteMessage(ScriptModule* module)
        : _module(module) { }

    void operator()(HotSwapScriptReloadMgr*)
    {
        _module.reset();
    }

private:
    std::unique_ptr<ScriptModule> _module;
};

void ScriptModule::ScheduleDelayedDelete(ScriptModule* module)
{
    sScriptReloadMgr->QueueMessage(ScriptModuleDeleteMessage(module));
}

/// Maps efsw actions to strings
static char const* ActionToString(efsw::Action action)
{
    switch (action)
    {
        case efsw::Actions::Add:
            return "added";
        case efsw::Actions::Delete:
            return "deleted";
        case efsw::Actions::Moved:
            return "moved";
        default:
            return "modified";
    }
}

void LibraryUpdateListener::handleFileAction(efsw::WatchID watchid, std::string const& dir,
    std::string const& filename, efsw::Action action, std::string oldFilename)
{
    // Split moved actions into a delete and an add action
    if (action == efsw::Actions::Moved)
    {
        ASSERT(!oldFilename.empty(), "Old filename doesn't exist!");
        handleFileAction(watchid, dir, oldFilename, efsw::Actions::Delete);
        handleFileAction(watchid, dir, filename, efsw::Actions::Add);
        return;
    }

    sScriptReloadMgr->QueueMessage([=](HotSwapScriptReloadMgr* reloader) mutable
    {
        auto const path = AbsolutePath(filename, HotSwapScriptReloadMgr::GetLibraryDirectory());

        if (!HasValidScriptModuleName(filename))
            return;

        switch (action)
        {
            case efsw::Actions::Add:
                LOG_TRACE("scripts.hotswap", ">> Loading \"{}\" ({})...",
                    path.generic_string(), ActionToString(action));
                reloader->QueueSharedLibraryChanged(path);
                break;
            case efsw::Actions::Delete:
                LOG_TRACE("scripts.hotswap", ">> Unloading \"{}\" ({})...",
                    path.generic_string(), ActionToString(action));
                reloader->QueueSharedLibraryChanged(path);
                break;
            case efsw::Actions::Modified:
                LOG_TRACE("scripts.hotswap", ">> Reloading \"{}\" ({})...",
                    path.generic_string(), ActionToString(action));
                reloader->QueueSharedLibraryChanged(path);
                break;
            default:
                ABORT();
                break;
        }
    });
}

/// Returns true when the given path has a known C++ file extension
static bool HasCXXSourceFileExtension(fs::path const& path)
{
    static std::regex const regex("^\\.(h|hpp|c|cc|cpp)$");
    return std::regex_match(path.extension().generic_string(), regex);
}

SourceUpdateListener::SourceUpdateListener(fs::path path, std::string scriptModuleName)
    : _path(std::move(path)), _scriptModuleName(std::move(scriptModuleName)),
      _watcherId(sScriptReloadMgr->_fileWatcher.addWatch(_path.generic_string(), this, true))
{
    if (_watcherId >= 0)
    {
        LOG_TRACE("scripts.hotswap", ">> Attached the source recompiler to \"{}\".",
            _path.generic_string());
    }
    else
    {
        LOG_ERROR("scripts.hotswap", "Failed to initialize the source recompiler on \"{}\".",
            _path.generic_string());
    }
}

SourceUpdateListener::~SourceUpdateListener()
{
    if (_watcherId >= 0)
    {
        sScriptReloadMgr->_fileWatcher.removeWatch(_watcherId);

        LOG_TRACE("scripts.hotswap", ">> Detached the source recompiler from \"{}\".",
            _path.generic_string());
    }
}

void SourceUpdateListener::handleFileAction(efsw::WatchID watchid, std::string const& dir,
    std::string const& filename, efsw::Action action, std::string oldFilename)
{
    // Skip the file change notification if the recompiler is disabled
    if (!IsRecompilerEnabled())
        return;

    // Split moved actions into a delete and an add action
    if (action == efsw::Actions::Moved)
    {
        ASSERT(!oldFilename.empty(), "Old filename doesn't exist!");
        handleFileAction(watchid, dir, oldFilename, efsw::Actions::Delete);
        handleFileAction(watchid, dir, filename, efsw::Actions::Add);
        return;
    }

    fs::path const path = AbsolutePath(filename, dir);

    // Check if the file is a C/C++ source file.
    if (!path.has_extension() || !HasCXXSourceFileExtension(path))
        return;

    /// Thread safe part
    sScriptReloadMgr->QueueMessage([=, this, path = std::move(path)](HotSwapScriptReloadMgr* reloader)
    {
        LOG_TRACE("scripts.hotswap", "Detected source change on module \"{}\", "
            "queued for recompilation...", _scriptModuleName);

        switch (action)
        {
            case efsw::Actions::Add:
                LOG_TRACE("scripts.hotswap", "Source file \"{}\" of module \"{}\" was added.",
                    path.generic_string(), _scriptModuleName);
                reloader->QueueAddSourceFile(_scriptModuleName, path);
                break;
            case efsw::Actions::Delete:
                LOG_TRACE("scripts.hotswap", "Source file \"{}\" of module \"{}\" was deleted.",
                    path.generic_string(), _scriptModuleName);
                reloader->QueueRemoveSourceFile(_scriptModuleName, path);
                break;
            case efsw::Actions::Modified:
                LOG_TRACE("scripts.hotswap", "Source file \"{}\" of module \"{}\" was modified.",
                    path.generic_string(), _scriptModuleName);
                reloader->QueueModifySourceFile(_scriptModuleName, path);
                break;
            default:
                ABORT();
                break;
        }
    });
}

// Returns the module reference of the given context
std::shared_ptr<ModuleReference>
    ScriptReloadMgr::AcquireModuleReferenceOfContext(std::string const& context)
{
    // A script name no registry knows belongs to no library at all.
    if (context.empty())
        return { };

    // Return empty references for the static context exported by the worldserver
    if (context == ScriptMgr::GetNameOfStaticContext())
        return { };

    auto const itr = sScriptReloadMgr->_runningScriptModules.find(context);
    ASSERT(itr != sScriptReloadMgr->_runningScriptModules.end(),
        "Requested a reference to a non existent script context!");

    return itr->second.first;
}

// Returns the full hot swap implemented ScriptReloadMgr
ScriptReloadMgr* ScriptReloadMgr::instance()
{
    static HotSwapScriptReloadMgr instance;
    return &instance;
}

#endif // ACORE_API_USE_DYNAMIC_LINKING
