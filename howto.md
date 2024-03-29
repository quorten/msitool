Creating a Windows Installer with `msi-tool`
============================================

Public Domain 2013, 2020 Andrew Makousky

See the file "UNLICENSE" in the top level directory for details.

`msi-tool` is a tool that helps facilitate the process of creating a
Windows Installer (MSI) package for installing a user application.
`msi-tool` was originally created with the intent of assisting in
creating a proof-of-concept Windows Installer package as fast as
possible.  Thus, the automation tool is not as polished as it could
be.  This document is a tutorial that will guide you through the
entire process of creating a Windows MSI installer package for a small
application with the help of `msi-tool`.

Prerequisites
=============

Before using `msi-tool`, you must have the Windows Platform SDK
installed on your computer.  It contains the necessary back-end tools
that will be used in creating the actual MSI installer package file.
The purpose of `msi-tool` is mainly to be a front-end for
automatically generating data files that will be needed by the
back-end tools.  You will have to manually run the back-end tools
after you use `msi-tool` to generate the intermediate data files.

For the general process of editing the tables within an MSI Installer
package, you should use the program Orca that comes with the Windows
Platform SDK.  Orca is not installed by default from the Windows
Platform SDK, but its installer package is included with the Windows
Platform SDK installation.  You will need to install Orca from its
installer package in order to use it.  Another program you will need
from the Windows Platform SDK that is not installed by default is
Msival2, which is used for validating installer packages.  You should
install this as well.

Background Information
======================

Readers are expected to have already read and understood the relevant
SDK documentation on Windows Installer.  However, for readers who are
not familiar with the SDK documentation on Windows installers, here
are some descriptions of major concepts that must be understood when
working with Windows Installer.

A Windows Installer package is essentially a database of tables along
with some other embedded data.  The tables contain the data that
Windows Installer will parse when preparing to install your software
package.  The data files for installation can be either located on
external media or within a cabinet file that is embedded within the
installer.  One table within a Windows Installer package specifies
*features*.  When a user goes through the user interface of an
installer package, at one point, the user may choose to install only
some parts of a package.  At the screen where the user can select what
to install and what not to install, the user is selecting *features*.
Now, this is where things get somewhat confusing.  Each "feature" is a
grouping of multiple *components*.  What's the different between a
"feature" and a "component"?  The most important difference between a
"feature" and a "component" for package authors is that a "component"
can only be comprised of installed files that are within the same
directory.  Thus, a "feature" can specify files from multiple
directories by specifying multiple "components".  A Windows Installer
package also specifies which directories will be needed by the
installer and which files will be installed in the form of tables.
The tables that specify files, directories, components, and features
are the tables that `msi-tool` generates.

There are also other important tables that you need for your
installer, such as a shortcuts table that specifies which shortcuts
will be created when the user installs your program.  This is how you
specify Start Menu shortcuts and Desktop shortcuts.  There are also
tables that specify the user interface.  However, `msi-tool` does not
create those tables automatically for you.  It is only the tables
related to features, components, directories, and files that
`msi-tool` automates the process of creating.

Getting Started
===============

The first thing you need to do to create a Windows Installer package
is get a default schema to base your installer package off of.  Since
`msi-tool` does not create user interface tables, you will need to get
an installer schema with a default user interface.  One such is
available from the Windows Platform SDK.  Within the Windows Platform
SDK directory, it's path is
"Samples\SysMgmt\Msi\database\UISample.msi".  Copy and rename this
file to begin creating your installer package.  Note that may be bugs
in the sample package's user interface.  Start by opening the
installer package with Orca to edit it.  In the `Property` table, find
the property named "DlgTitleFont" and changed its value to
"{\DlgFontBold8}".  In the `Control` table, select the table row that
contains "VerifyRemoveDlg" in the "Dialog" column and "Title" in the
"Control" column.  Scroll right until you see the "Text" column within
that row.  Now change its value to
"{\DlgFontBold8}Remove [ProductName]".  The last bug in the installer
package may be that the image sizes for the installer's user interface
aren't quite perfect.  The only images that might be issues are the
banner bitmap, which should be 500x58 pixels in size, and the main
dialog bitmap, which should be 503x314 pixels in size.  Note that the
banner and main dialog bitmaps are located in the `Binary` table
within the Windows Installer package, so this is where you should go
in Orca if you want to customize those bitmaps.

Bugs aside, you will need to modify the default schema's user
interface a bit before it is suited for your project.  Two things in
particular that you will want to tweak is the license dialog and the
registration dialog.  For the license dialog, you will want to insert
your license text by modifying the releveant control, which is the row
in the `Control` table with the `Dialog` column as
`LicenseAgreementDlg` and `Control` column as `AgreementText`.  For
the registration dialog, you might want to set the
`ShowUserRegistrationDlg` property in the `Properties` table to `0`
(zero).

After getting the schema, the next thing to do when creating a Windows
Installer is to initialize some entries within the `Property` table.
Here is what you need in the `Property` table:

    Property       Value
    False          0
    True           1
    Manufacturer   YOUR-ORGANIZATION
    ProductCode    {GUID}
    ProductName    YOUR-PRODUCT
    ProductVersion 0.1
    UpgradeCode    {GUID}

Now you will want to make sure the summary information for the
installer is correct.  Here is an example of what you might want for
the summary information:

* Title: Installation Database
* Author: Your Name Here
* Subject: YOUR-PRODUCT
* Comments: This installer database contains the logic and data required to install YOUR-PRODUCT.
* Keywords: Installer, MSI, Database
* Platform: Intel
* Languages: 1033
* Package Code: {YOUR-GUID-HERE}
* Schema: 110
* Security: Your Choice
* Long File Names
* Compressed by Default

Using `msi-tool`
================

The next step is to specify the files and directories used for your
installation, break up your installation into components, and
associate those components with features.  These steps are the steps
that are automated by `msi-tool`.  Here are the steps that you will
have to do:

1. Build the directory listings.

Start by building the directory tree of files that your installer will
install into the target system.  To make the process of breaking the
installation up into multiple features easier, you may want to
separate this installation directory tree into multiple trees, one for
each major feature.  Make sure that there are no empty directories
within your directory trees.  Now for each top level directory, run

    ls -RF DIRNAME > DIRNAME.txt

to get a directory listing of that directory.  You will need to ommit
subdirectories and empty directories from this listing and get rid of
any special characters appended to the end of other files within the
directory listing.  To perform the process more completely, use the
following command:

    ls -RF DIRNAME | \
      sed -e '/^.*\/$/d' -e 's/\(^.*\)\*/\1/g' > DIRNAME.txt

Note that the above command only removes the special character
appended onto the end of executable files.  If you need to remove
other special characters that are appended to filenames, you will need
to add those rules to the sed script.  One last tip: you will probably
want to make sure the `ls -R` files have Unix newlines and not DOS
newlines.

2. Create the feature specification file.

Create an empty text file called "features.txt".  This is the file
that you will use to specify features.  Basically, you will be writing
which features are associated with which *directories*.  `msi-tool`
will automate the process of creating components as necessary and
associating those components with directories and features.  Suppose
that you created a GTK+ application and you have `ls -R` listings for
two separate directories: one for the GTK+ runtime files and one for
your own application's files.  In that case, the features file should
look something like this:

```
GTK+ Runtime Library:
	2.12.10-dist/bin
	2.12.10-dist/etc
	2.12.10-dist/lib
	2.12.10-dist/share/doc
	2.12.10-dist/share/themes
	GTK+ Languages:
		es:
			2.12.10-dist/share/locale/es
		fr:
			2.12.10-dist/share/locale/fr
Sound Studio:
	sndstud/bin
	sndstud/share/locale
	sndstud/share/sndstud/pixmaps/icon16.png
	sndstud/share/sndstud/pixmaps/icon32.png
	sndstud/share/sndstud/pixmaps/icon48.png
Sound Studio Extras:
	sndstud/share/sndstud/pixmaps/icon256.png
```

Note that the format is similar to an `ls -R` listing, except that
there are tabs throughout the file to indicate nesting level.  Each
header with a colon is the name of the feature.  This is what the user
will see in the user interface.  Subfeatures are specified using tabs
for nesting.  Within each feature, a file or directory may be
specified.  The contents of a directory will be recursively included,
automatically including any subdirectories within that directory.
Note that wildcards are not expanded within filenames.  If you specify
individual filenames rather than directories, make sure you are
careful to specify filenames within the same directory adjacent to
each other so that `msi-tool` will generate optimal results.  Keeping
the path names in standard sort order within a feature will take care
of this issue.

When you finish creating this file, make sure that this file also has
Unix newlines.

3. Create the UUID cache file.

Now you will need a file containing a whole bunch of UUIDs for
`msi-tool` to grab from.  Use

    uuidgen -c -n1000 > uuids.txt

to generate 1000 UUIDs.  The number of UUIDs you will need to generate
will depend on the number of "components" that your installer will
need, which depends on the number of directories within your
installation's directory structure.  The reason why the UUID
generation code isn't directly within `msi-tool` is so that you will
reuse already generated UUIDs rather than keep generating new ones
when you don't strictly need a new one.

4. Run `msi-tool` to generate the tables.

Briefly, you should run `msi-tool` without any command-line arguments.
All the files that `msi-tool` needs should be in the current working
directory.  Right now, `msi-tool` only supports reading in strictly
two `ls -R` files, one named "ls-r.txt" and the other named
"ls-r2.txt".  The features file must be named "features.txt" as stated
above.  The UUID file must also be properly named.

    msi-tool -d"sndstud|Sound Studio" ls-r.txt ls-r2.txt

If you think you are really ready to build the installer, run
`msi-tool` with this command line:

    msi-tool -r -d"sndstud|Sound Studio" ls-r.txt ls-r2.txt

The `-r` argument tells `msi-tool` that it should rename files to
their short names while it generates the tables.  This switch is
mainly used for building an embedded cabinet file.  After you run this
command, each of your `ls -R` directories will have all of their files
renamed to their short names and moved to the root of the first `ls -R
directory`.  The leftover subdirectory structure is nothing but empty
directories.  `msi-tool` also generated a file named `cablist.txt`
within the first `ls -R` directory, which is used to specify the file
names that should be archived.

5. Edit the generated tables.

After running `msi-tool`, the files "Component.idt", "Directory.idt",
"Feature.idt", "FeatureComponents.idt", "File.idt", and "Media.idt"
will be generated within the current working directory.  Note that
`msi-tool` was designed to automate the most repetitive parts of
manually creating a Windows Installer rather than be an automated
build tool of its own.  Thus, you are going to have to manually edit
the generated tables before they can be merged with the installer.

5a. Edit the `Directory` table.

In earlier versions of `msi-tool`, you had to add essential directory
rows into the `Directory` table manually.  Now all the essential
directory rows are added automatically.  Here are some convenient,
though unnecessary, rows that you may want to add to your table:

    Directory               Directory Parent    DefaultDir
    DesktopFolder           TARGETDIR           .
    StartMenuFolder         TARGETDIR           .
    ProductShortcutFolder   StartMenuFolder     sndstud|Sound Studio

5b. Edit the `Feature` table.

You will need to to change the default values that `msi-tool` put into
the `Description`, `Display`, `Level` columns of the `Feature` table.
The `Description` column should contain a long description of the
given feature.  Briefly, there are two simple modifications you will
want to make to the `Display` and `Level` columns.  If you want a
feature node to be initially displayed as expanded, decrease the value
of that feature's `Display` attribute by one so that it is an odd
number.  The default even number means that the feature node will be
initially displayed as collapsed and have its subfeatures hidden.  If
you want a feature to not be installed by default, change that
feature's `Level` attribute from 3 to 4.  See the Windows Platform SDK
documentation for full details of these column values.

6. Merge the tables into the MSI installer package.

Once you have finished modifying the generated tables, you can merge
the tables into your database using the following command line:

    msidb -dDATABASE.msi -f{PATH} -i Component.idt Directory.idt Feature.idt \
      FeatureComponents.idt File.idt Media.idt

Note that due to inflexibility within `msidb`, `{PATH}` must
correspond to the absolute path to the current working directory.

7. Create and merge a cabinet file.

If you want all of the install data that the installer needs to be
part of the installer package, then you will need to pack it into a
cabinet file using `cabarc`, which is part of the Platform SDK.

Make sure you have run `msi-tool` with the `-r` argument as described
earlier in this document.  After you do that, you should delete the
empty subdirectories within the `ls -R` directories, change the
current working directory to the first root directory, then run
`cabarc` like so:

    cabarc -m LZX:21 n archive.cab @cablist.txt

The cabinet file `archive.cab` is the file that you will merge into
your installer.  Move the cabinet file out of the subdirectory that
you generated it in, then merge it into the installer package with the
following command:

    msidb -dDATABASE.msi -aarchive.cab

Finalizing the Installer
========================

The entire process of creating your Windows Installer is almost
complete.  Here are the few finishing touches you will make in order
to complete the process.

You probably want your installer to create start menu shortcuts and
desktop shortcuts for the user.  The process for doing this is
documented in the Windows Platform SDK, but for convenience, here is a
brief summary of what you need to do.

Start by creating a new table named `Shortcut`.  Using the same
example application that has been used throughout this document, here
is what you would need to put in that table to create one shortcut for
the Start Menu and one shortcut for the desktop:

    Shortcut         Directory              Name              Component  Target  Icon
    sSndStudStart    ProductShortcutFolder  sc0|Sound Studio  c115       ft101   sndstud_icon.exe
    sSndStudDesktop  DesktopFolder          sc1|Sound Studio  c115       ft101   sndstud_icon.exe

Any column headers not mentioned should be left empty.  Note that the
exact component and feature association is your choice.  Also note
that the value of the `Icon` column must refer to an entry within the
`Icon` table.  You will need to create that table.  You will also need
to create a `RemoveFile` table:

    FileKey  Component  FileName  DirProperty            InstallMode
    rf0      c115                 ProductShortcutFolder  2

The last thing you need to do is validate your installer with
`msival2` to make sure that the Windows Installer is properly formed.
After your installer passes validation with no errors, your installer
is complete.
