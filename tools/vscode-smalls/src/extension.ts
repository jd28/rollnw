import * as path from 'path';
import * as fs from 'fs';
import * as os from 'os';
import { workspace, ExtensionContext, window } from 'vscode';

import {
	LanguageClient,
	LanguageClientOptions,
	ServerOptions,
	TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient;

export function activate(context: ExtensionContext) {
    const config = workspace.getConfiguration('smalls');
    let serverPath: string = config.get('lsp.path') || 'smalls-lsp';

    // If the user hasn't explicitly set a path, check for a bundled binary
    if (!config.get('lsp.path') || config.get('lsp.path') === 'smalls-lsp') {
        const platform = os.platform(); // 'win32', 'linux', 'darwin'
        const arch = os.arch();         // 'x64', 'arm64'
        const ext = platform === 'win32' ? '.exe' : '';
        const binaryName = `smalls-lsp-${platform}-${arch}${ext}`;
        
        const bundledPath = context.asAbsolutePath(path.join('bin', binaryName));
        if (fs.existsSync(bundledPath)) {
            serverPath = bundledPath;
        } else {
            // Fallback for local development if the exact platform name isn't built
            const fallbackPath = context.asAbsolutePath(path.join('bin', `smalls-lsp${ext}`));
            if (fs.existsSync(fallbackPath)) {
                serverPath = fallbackPath;
            }
        }
    }

    const args: string[] = [];

    const workspaceRoot = workspace.workspaceFolders?.[0]?.uri.fsPath;
    const seenModulePaths = new Set<string>();

    const addModulePath = (modulePath: string) => {
        if (!seenModulePaths.has(modulePath)) {
            seenModulePaths.add(modulePath);
            args.push("-I", modulePath);
        }
    };

    if (workspaceRoot) {
        const workspaceStdlib = path.join(workspaceRoot, 'lib', 'nw', 'smalls', 'scripts');
        if (fs.existsSync(workspaceStdlib)) {
            addModulePath(workspaceStdlib);
        }
    }

    const modulePaths: string[] = config.get('modulePaths') || [];

    const relativeBases: string[] = [];
    if (workspaceRoot) {
        relativeBases.push(workspaceRoot);
    }
    relativeBases.push(
        context.extensionPath,
        path.resolve(context.extensionPath, '..'),
        path.resolve(context.extensionPath, '../..'),
        process.cwd()
    );

    for (const configuredPath of modulePaths) {
        let resolvedPath = configuredPath;

        if (!path.isAbsolute(configuredPath)) {
            for (const base of relativeBases) {
                const candidate = path.resolve(base, configuredPath);
                if (fs.existsSync(candidate)) {
                    resolvedPath = candidate;
                    break;
                }
            }
        }

        addModulePath(resolvedPath);
    }

	// If the extension is launched in debug mode then the debug server options are used
	// Otherwise the run options are used
	let serverOptions: ServerOptions = {
		run: { command: serverPath, args: args, transport: TransportKind.stdio },
		debug: { command: serverPath, args: args, transport: TransportKind.stdio }
	};

	// Options to control the language client
	let clientOptions: LanguageClientOptions = {
		// Register the server for plain text documents
		documentSelector: [{ scheme: 'file', language: 'smalls' }],
		synchronize: {
			// Notify the server about file changes to '.clientrc files contained in the workspace
			fileEvents: workspace.createFileSystemWatcher('**/*.smalls')
		}
	};

	// Create the language client and start the client.
	client = new LanguageClient(
		'smallsLsp',
		'Smalls Language Server',
		serverOptions,
		clientOptions
	);

	// Start the client. This will also launch the server
	client.start().catch(err => {
        window.showErrorMessage(`Failed to start Smalls Language Server: ${err}`);
    });
}

export function deactivate(): Thenable<void> | undefined {
	if (!client) {
		return undefined;
	}
	return client.stop();
}
