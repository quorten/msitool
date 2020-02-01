msi-tool README
===============

`msi-tool` is a tool that helps facilitate the process of creating a
Windows Installer (MSI) package for installing a user application.
`msi-tool` was originally created with the intent of assisting in
creating a proof-of-concept Windows Installer package as fast as
possible.  Thus, the automation tool is not as polished as it could
be.  For a complete guided tutorial on how to use `msi-tool` in the
process of creating a Windows Installer, see `howto.md`.

Why?
====

Okay, so to answer the "why?" question with a more detailed
explanation.  Like, why not just use WiX to build your MSI files?
Well, first of all, WiX is .NET, and that means you've got to bring in
more dependencies to run it.  `msi-tool` is plain C software that can
be used with the `.exe` binary tools readily available in the Windows
Platform SDK For Windows Server 2003 R2.  And, most importantly, I was
not connected to a sufficient development community at the time for
the "word of mouth" of the WiX tool to make its way to me.  So I went
about writing my own such tool since it seemed to me all I needed was
a simple tool to remove the tediousness of manually authoring all the
needed tables to input to the MSI file.
