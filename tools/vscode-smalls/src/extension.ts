import * as path from 'path';
import * as fs from 'fs';
import * as os from 'os';
import {
    commands,
    ExtensionContext,
    LogOutputChannel,
    window,
    workspace,
    WorkspaceFolder,
} from 'vscode';

import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;
let outputChannel: LogOutputChannel | undefined;

/// Locates the server binary.
///
/// A path the user set explicitly always wins. `inspect` is what distinguishes
/// "unset" from "set to the default string": a user who writes `smalls-lsp`
/// means "find it on PATH" and must not be overridden by a bundled build.
function resolveServerPath(context: ExtensionContext): string {
    const setting = workspace.getConfiguration('smalls').inspect<string>('lsp.path');
    const explicit =
        setting?.workspaceFolderValue ?? setting?.workspaceValue ?? setting?.globalValue;
    if (explicit && explicit.length > 0) {
        return explicit;
    }

    const platform = os.platform();
    const arch = os.arch();
    const exeSuffix = platform === 'win32' ? '.exe' : '';

    const candidates: string[] = [];
    if (platform === 'darwin') {
        candidates.push('smalls-lsp-darwin-universal');
    }
    candidates.push(`smalls-lsp-${platform}-${arch}${exeSuffix}`);
    candidates.push(`smalls-lsp${exeSuffix}`);

    for (const candidate of candidates) {
        const bundled = context.asAbsolutePath(path.join('bin', candidate));
        if (fs.existsSync(bundled)) {
            ensureExecutable(bundled);
            return bundled;
        }
    }

    return setting?.defaultValue ?? 'smalls-lsp';
}

/// A VSIX is a zip, and the executable bit does not reliably survive packaging.
/// Without this the bundled server fails to spawn with EACCES.
function ensureExecutable(binary: string): void {
    if (os.platform() === 'win32') {
        return;
    }
    try {
        fs.accessSync(binary, fs.constants.X_OK);
    } catch {
        try {
            fs.chmodSync(binary, 0o755);
        } catch (err) {
            outputChannel?.appendLine(`Could not mark ${binary} executable: ${err}`);
        }
    }
}

/// Expands `${workspaceFolder}` and resolves relative entries against the
/// workspace only. Resolving against the extension directory or the extension
/// host's working directory produces results that depend on how VS Code was
/// launched.
function resolveConfiguredPath(configured: string, folders: readonly WorkspaceFolder[]): string[] {
    const resolved: string[] = [];

    for (const folder of folders) {
        const substituted = configured.replace(/\$\{workspaceFolder\}/g, folder.uri.fsPath);
        if (path.isAbsolute(substituted)) {
            resolved.push(substituted);
        } else {
            resolved.push(path.resolve(folder.uri.fsPath, substituted));
        }
    }

    if (folders.length === 0 && path.isAbsolute(configured)) {
        resolved.push(configured);
    }

    return resolved;
}

/// Expands a directory into the package roots the server expects.
///
/// A module path is registered as a flat directory of sources, so it must be
/// the directory that directly contains the `.smalls` files. A package marks
/// itself with a `package.json`, and the package name comes from the directory
/// holding it: `<root>/core/array.smalls` is `core.array`, so `<root>/core` is
/// what gets registered, not `<root>`.
function addPackageRoots(dir: string, add: (candidate: string) => void): void {
    if (!fs.existsSync(dir)) {
        return;
    }
    if (fs.existsSync(path.join(dir, 'package.json'))) {
        add(dir);
        return;
    }

    let entries: fs.Dirent[];
    try {
        entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch {
        return;
    }

    let found = false;
    for (const entry of entries) {
        if (!entry.isDirectory()) {
            continue;
        }
        const child = path.join(dir, entry.name);
        if (fs.existsSync(path.join(child, 'package.json'))) {
            add(child);
            found = true;
        }
    }
    if (found) {
        return;
    }

    // A plain directory of sources with no manifest is its own root. A
    // directory that declares no package and holds no sources is not: adding a
    // workspace root that merely contains packages far below makes every file
    // resolve under the deep path instead of its package, so `core.array`
    // becomes `lib.nw.smalls.scripts.core.array` and stops matching the native
    // module registry.
    if (entries.some((entry) => entry.isFile() && entry.name.endsWith('.smalls'))) {
        add(dir);
    }
}

/// Builds the module search path in a documented precedence order:
/// configured paths, then workspace folders, then the bundled stdlib.
function collectModulePaths(context: ExtensionContext): string[] {
    const folders = workspace.workspaceFolders ?? [];
    const seen = new Set<string>();
    const result: string[] = [];

    const add = (candidate: string) => {
        if (!candidate || seen.has(candidate) || !fs.existsSync(candidate)) {
            return;
        }
        seen.add(candidate);
        result.push(candidate);
    };

    const configured = workspace.getConfiguration('smalls').get<string[]>('modulePaths') ?? [];
    for (const entry of configured) {
        for (const candidate of resolveConfiguredPath(entry, folders)) {
            addPackageRoots(candidate, add);
        }
    }

    for (const folder of folders) {
        addPackageRoots(folder.uri.fsPath, add);
        // Developing inside the rollnw checkout itself: the stdlib lives in the
        // source tree rather than in the packaged extension.
        addPackageRoots(path.join(folder.uri.fsPath, 'lib', 'nw', 'smalls', 'scripts'), add);
    }

    // Shipped with the VSIX, so a published extension resolves `core.` imports
    // outside this repository.
    addPackageRoots(context.asAbsolutePath('stdlib'), add);

    return result;
}

/// Inlay hints are divisive, so each category is switchable on its own. A user
/// who cannot turn one off turns all of them off.
function inlayHintSettings() {
    const config = workspace.getConfiguration('smalls.inlayHints');
    return {
        parameterNames: config.get<boolean>('parameterNames', true),
        variableTypes: config.get<boolean>('variableTypes', true),
        foreachTypes: config.get<boolean>('foreachTypes', true),
        lambdaReturnTypes: config.get<boolean>('lambdaReturnTypes', true),
    };
}

function currentSettings(context: ExtensionContext) {
    return {
        smalls: {
            modulePaths: collectModulePaths(context),
            inlayHints: inlayHintSettings(),
        },
    };
}

async function startClient(context: ExtensionContext): Promise<void> {
    const serverPath = resolveServerPath(context);
    const modulePaths = collectModulePaths(context);

    const args: string[] = [];
    for (const modulePath of modulePaths) {
        args.push('-I', modulePath);
    }

    const serverOptions: ServerOptions = {
        run: { command: serverPath, args, transport: TransportKind.stdio },
        debug: {
            command: serverPath,
            args,
            transport: TransportKind.stdio,
            options: { env: { ...process.env, SMALLS_LSP_VERBOSE: '1' } },
        },
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [
            { scheme: 'file', language: 'smalls' },
            { scheme: 'untitled', language: 'smalls' },
        ],
        outputChannel,
        // Server-side diagnostics belong in the output pane, not in a modal.
        revealOutputChannelOn: 4,
        initializationOptions: { modulePaths, inlayHints: inlayHintSettings() },
        synchronize: {
            fileEvents: workspace.createFileSystemWatcher('**/*.smalls'),
        },
    };

    client = new LanguageClient('smallsLsp', 'Smalls Language Server', serverOptions, clientOptions);

    try {
        await client.start();
    } catch (err) {
        window.showErrorMessage(
            `Failed to start Smalls Language Server (${serverPath}): ${err}`,
        );
        client = undefined;
        return;
    }

    await client.sendNotification('workspace/didChangeConfiguration', {
        settings: currentSettings(context),
    });
}

async function stopClient(): Promise<void> {
    const running = client;
    client = undefined;
    if (!running) {
        return;
    }
    try {
        await running.stop();
    } catch {
        // The process may already be gone; a restart must not be blocked by it.
    }
}

export async function activate(context: ExtensionContext): Promise<void> {
    outputChannel = window.createOutputChannel('Smalls Language Server', { log: true });
    context.subscriptions.push(outputChannel);

    context.subscriptions.push(
        commands.registerCommand('smalls.restartServer', async () => {
            await stopClient();
            await startClient(context);
        }),
    );

    // Module paths are server state, so a settings change is pushed rather than
    // requiring a window reload.
    context.subscriptions.push(
        workspace.onDidChangeConfiguration(async (event) => {
            if (!event.affectsConfiguration('smalls')) {
                return;
            }
            if (event.affectsConfiguration('smalls.lsp.path')) {
                await stopClient();
                await startClient(context);
                return;
            }
            await client?.sendNotification('workspace/didChangeConfiguration', {
                settings: currentSettings(context),
            });
        }),
    );

    context.subscriptions.push(
        workspace.onDidChangeWorkspaceFolders(async () => {
            await client?.sendNotification('workspace/didChangeConfiguration', {
                settings: currentSettings(context),
            });
        }),
    );

    await startClient(context);
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return stopClient();
}
