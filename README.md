For the documentation, see the original project: https://github.com/apache/subversion
 
# Modified svnlook
 
Added a meta subcommand that is coherent with the behavior of cat regarding releases. It shows information of a path
as it existed at the time of the specified revision, whether it is a file or a directory.

'svn meta' returns metadata on the path. When no revision is given, the latest one
is taken. If a revision is specified, the path given can be either the current
one if it exists (the function looks up the historical one) or the path that
existed at the moment the revision was made.

# Rationale for the modification

'svn info' needs an existing path in order to return info on old versions of
that path. If a path doesn't exist anymore, specifying a revision will not help
and no info is returned. 'svnlook info' on the other hand doesn't return info on paths, only on releases.

'svnlook meta' is a modification that gives info on paths as they are at the time of the revision. 
 
# Compiling on Centos 7
 
     # git clone https://github.com/rveen/subversion.git
     # cd subversion
     # ./autgen.sh
     # wget https://www.sqlite.org/2015/sqlite-amalgamation-3081101.zip
     # unzip sqlite-amalgamation-3081101.zip
     # mv sqlite-amalgamation-3081101 sqlite-amalgamation
     # ./configure  --prefix=/opt/svn --without-apxs --with-lz4=internal --with-utf8proc=internal
     # make; make install
    
# Compiling on Fedora 34
 
     # git clone https://github.com/rveen/subversion.git
     # cd subversion
     # ./autgen.sh
     # wget https://www.sqlite.org/2015/sqlite-amalgamation-3081101.zip
     # unzip sqlite-amalgamation-3081101.zip
     # mv sqlite-amalgamation-3081101 sqlite-amalgamation
     # dnf install apr-devel apr-util-devel autoconf libtool
     # ./configure  --prefix=/opt/svn --without-apxs --with-lz4=internal --with-utf8proc=internal
     # make; make install
     
The files will be installed in /opt/svn. Only the svnlook command is modified with respect to upstream.
