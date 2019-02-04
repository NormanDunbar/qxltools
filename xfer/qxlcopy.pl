#!/usr/bin/perl
#########################################################################
# Copy all files from a QXL.WIN file into an arbitrary directory
# in the _native_ filesystem.
#
# e.g.
#
# qxlcopy.pl /tmp/qxl.win /tmp/qlfiles			(Unix)
# perl qxlcopy.pl \temp\qxl.win \temp\qlfiles		(DOS et al)
# ex perl;'win1_qxlcopy.pl win2_tmp_qxl.win win5_'	(QDOS)
#########################################################################

$qxl = shift || die "qxl file required\n";
$dir = shift || die "Directory required\n";

if(!defined($^O))
{
  $^O = `uname -s`;
}
$^O = 'QDOS' unless (defined($^O));

SW:
{
  ($^O =~ /QDOS/) && do
  {
    $sep = '_';
    last SW;
  };
  $sep = '/';
}

open(F, "qxltool -c lslr ".$qxl."|") || die "Can't run qxltool, alas";
mkdir ($dir,0777);
@files = <F>;
close(F);

open(F,"|qxltool ".$qxl) || die "Can't run qxltool, alas";

foreach (@files)
{
  tr[\r\n][]d;
  @items = split;
  if($items[2] =~ /\(Directory\)/)
  {
    $dn = $items[0];
    if($^O ne "QDOS")
    {
      $dn =~ s/_/$sep/g;
    }
    mkdir ($dir.$sep.$dn, 0777);
    push (@ldir,$dn);
  }
  else
  {
    $pos = 0;
    while(($n = index ($items[0],"_", $pos)) != -1)
    {
      $td =substr($items[0], 0, $n);
      @f = grep (/^${td}$/i, @ldir);
      if(defined ($f[0]))
      {
	substr ($items[0], 0, length($f[0])) = $f[0];
	substr ($td, 0, length($f[0])) = $f[0];
      }
      if($^O ne "QDOS")
      {
	$td =~ s/_/$sep/g;
      }
      $td = $dir . $sep . $td;
      last unless (-d $td);
      $pos = $n+1;
    }
    if($pos > 0)
    {
      $od = substr($items[0], 0, $pos-1);
      $fn = substr($items[0], $pos);
      if($^O ne "QDOS")
      {
	$od =~ s/_/$sep/g;
      }
      print F "cp ".$items[0]." >".$dir.$sep.$od.$sep.$fn,"\n";
    }
    else
    {
      print F  "cp ".$items[0]." >".$dir.$sep.$items[0],"\n";
    }
  }
}
close(F);
