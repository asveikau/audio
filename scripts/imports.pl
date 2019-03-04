use strict;

my %modules;
my $currentModule;

my $id = 0;

while (<>)
{
   chomp;
   s/#.*$//;
   s/^\s*//;
   s/\s*$//;
   next if ("$_" eq "");
   if (/\[\s*(.*)\s*\]/)
   {
      my $modname = $1;
      my @array;
      $currentModule = $modules{$modname} = \@array;
   }
   else
   {
      die "No current module" if (!defined $currentModule);

      my $record = {};

      if (/,(.*)/)
      {
         $record->{flags} = $1;
         s/,.*$//;
      }

      if (/(@[0-9]*)$/)
      {
         $record->{decor} = $1;
         s/@[0-9]*$//;
      }

      if (/(.*)=(.*)/)
      {
         $record->{func} = $1;
         $record->{target} = $2;
      }
      else
      {
         $record->{func} = $record->{target} = $_;
      }

      $record->{id} = $id;
      $id++;

      push (@$currentModule, $record);
   }
}


my %error_emit = {};

my $key;
foreach $key (keys %modules)
{
   my $value = $modules{$key};
   foreach $value (@$value)
   {
      if (defined $value->{flags})
      {
         if ((!defined $error_emit{"win32"}) && ($value->{flags} =~ /win32/))
         {
            if (!defined $error_emit{"text"})
            {
               print "section .text\n";
               $error_emit{"text"} = 1;
            }
            print "extern _SetLastError\@4\n";
            print "sle:\n";
            print "   push dword 50\n";
            print "   call _SetLastError\@4\n";
            print "   xor eax,eax\n";
            print "   ret\n";
            $error_emit{"win32"} = 1;
         }

         my $symbol = "error_$value->{flags}$value->{decor}";
         $value->{error} = "$symbol";
         if (!defined $error_emit{$symbol})
         {
            if (!defined $error_emit{"text"})
            {
               print "section .text\n";
               $error_emit{"text"} = 1;
            }

            print "$symbol:\n";

            if ($value->{flags} eq "win32bool")
            {
               print "   call sle\n";
            }
            elsif ($value->{flags} eq "com")
            {
               print "   mov eax, 80004001h\n";
            }

            my $decor = $value->{decor};
            $decor =~ s/@//;
            print "   ret $decor\n";
            $error_emit{$symbol} = 1;
         }
      }
   }
}

print "section .data\n";

print "init_state:\n";
print "   dd 0\n";

foreach $key (keys %modules)
{
   my $value = $modules{$key};
   foreach $value (@$value)
   {
      print "ptr_$value->{id}:\n";
      if (defined $value->{error})
      {
         print "   dd $value->{error}\n";
      }
      else
      {
         print "   dd 0\n";
      }
   }
}

print "section .rdata\n";

$id = 0;
foreach $key (keys %modules)
{
   print "str_mod_$id:\n";
   $id++;
   print "   db '$key', 0\n";

   my $value = $modules{$key};
   foreach $value (@$value)
   {
      print "str_fn_$value->{id}:\n";
      print "   db '$value->{target}', 0\n";
   }
}

print "align 4\n";

$id = 0;
foreach $key (keys %modules)
{
   print "modlist_$id:\n";
   $id++;

   my $value = $modules{$key};
   foreach $value (@$value)
   {
      print "   dd str_fn_$value->{id}\n";
      print "   dd ptr_$value->{id}\n";
   }
   print "   dd 0, 0\n";
}

print "modules:\n";

$id = 0;
foreach $key (keys %modules)
{
   print "   dd str_mod_$id\n";
   print "   dd modlist_$id\n";
   $id++;
}
print "   dd 0, 0\n";

print "section .text\n";

print "extern _lazy_init\n";
print "extern _error_clear\n";
print "extern _load_imports\n";
print "init:\n";
print "   push edi\n";
print "   sub esp, 5*4\n";
print "   mov ecx, 5*4\n";
print "   xor eax, eax\n";
print "   mov edi, esp\n";
print "   repnz stosb\n";
print "   push esp\n";
print "   push modules\n";
print "   push _load_imports\n";
print "   push init_state\n";
print "   call _lazy_init\n";
print "   add esp, 4*4\n"; 
print "   push esp\n";
print "   call _error_clear\n";
print "   add esp, 4 + 5*4\n";
print "   pop edi\n";
print "   ret\n";

foreach $key (keys %modules)
{
   my $value = $modules{$key};
   foreach $value (@$value)
   {
      my $name = $value->{func};
      if (defined $value->{decor})
      {
         $name = "_$name$value->{decor}";
      }
      print "global $name\n";
      print "$name:\n";
      print "   call init\n";
      print "   mov eax, dword [ptr_$value->{id}]\n";
      print "   jmp eax\n";
   }
}

