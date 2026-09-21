# Your first GitHub upload

Updated: 11 September 2026, 16:17 CEST (Europe/Berlin).

The prepared folder is a local Git repository with a clean initial history. Nothing has been uploaded. Use this sharing folder, not the original development folder. You do not need to compile Archicad code to upload it.

## Publish with GitHub Desktop

1. Create/sign in to your GitHub account, install GitHub Desktop and sign in there.
2. In GitHub Desktop choose **File → Add Local Repository**. Select the prepared `tapir-ai-github` folder.
3. Open **History** to see the prepared initial commit. Review the README and status document.
4. Click **Publish repository**. Choose your account and a repository name, for example `tapir-ai-interface`.
5. For public community collaboration, untick **Keep this code private**. Read the selected visibility, then click **Publish repository** when ready.
6. Open the repository on GitHub and copy its web address. Share that address with the developers you want to involve.

GitHub documents [adding a local repository](https://docs.github.com/en/desktop/adding-and-cloning-repositories/adding-a-repository-from-your-local-computer-to-github-desktop) and [publishing an existing project](https://docs.github.com/en/desktop/adding-and-cloning-repositories/adding-an-existing-project-to-github-using-github-desktop).

## Collaborating after publication

Other developers can open issues or propose changes through pull requests. They do not need permission to overwrite your repository to do that. Start by asking for review of the native integration and known limitations in `docs/STATUS.md`. Review proposed changes before merging them.

Use the sharing repository for future public development. If further changes are made in the private working folder, transfer only reviewed code and documentation changes; never copy its `.git` folder or private files into this repository. GitHub Desktop will show changes for the next commit and push.

The ZIP is a source backup: uploading it as one ZIP file would not give collaborators a browsable source repository. Use the prepared local repository for the steps above.

This package starts an independent repository derived from Tapir. If you prefer a GitHub fork directly linked to Tapir upstream, first create that fork and then transfer the reviewed changes onto its history. Do not force-push this snapshot over an upstream fork.
