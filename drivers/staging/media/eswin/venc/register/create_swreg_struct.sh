#!/bin/bash
#Copyright 2019 Verisilicon(Beijing) Co.,Ltd. All Rights Reserved.
#Author: carl.li@verisilicon.com (Carl Li)
#
#Description: Creates code for various purposes from
#             given swhw register definition

if [ -z "$1" ] || [ ! -e "$1" ]
then
        echo " This script produces swregisters.h, swregisters_directives.tcl,"
        echo " hwdriver.vhd, signals.vhd, 8170table.h and 8170enum.h files"
        echo " from given .csv register description"
        echo ""
        echo "Usage: ./create_swreg_struct.sh 'filename.csv' "
        exit
fi

fileName=$1
version=`cat $1 | grep "Document Version" | tr "," " "| awk '{ print $3}'`

if [ "$version" == "" ]
then
  echo "Document $1 version not found. Exit..." 
  exit
else
  echo "Creating swregister struct from $1 version $version"
fi
vp9_output="$2"

printf "%-8s\n" $vp9_output;

catapultPrefix=SwRegister_
catapultPostfix=""   ##empty (if interface label to direct input made correctly
if [ "$vp9_output" != "vp9" ]
then
awk '
        BEGIN{FS=",";printf "//Copyright 2019 Verisilicon(Beijing) Co.,Ltd. All Rights Reserved.\n"> "swregisters.h";
                              printf "//Author: carl.li@verisilicon.com (Carl Li)\n">> "swregisters.h";
                              printf "//\n">> "swregisters.h";
                              printf "//Description: Common SWHW interface structure definition\n">> "swregisters.h";
                              printf "//Based on document version '$version'\n">> "swregisters.h";
                              printf "#ifndef SWREGISTERS_H_\n">> "swregisters.h";
                              printf "#define SWREGISTERS_H_\n\n">> "swregisters.h";
                              printf "#include \"actypes.h\"\n\n">> "swregisters.h";
                              printf "struct SwRegisters {\n">> "swregisters.h";
                              printf"--signal declaration from Document Version '$version'\n" > "signals.vhd";
                              printf"--register to signal map table from Document Version '$version'\n" > "hwdriver.vhd"
                              printf"##Common catapult directives from Document Version '$version' \n" > "swregisters_directives.tcl"
                              printf "/* Copyright 2019 Verisilicon(Beijing) Co.,Ltd. All Rights Reserved. */\n"> "8170enum.h";
                              printf "/* Register interface based on the document version '$version' */\n">> "8170enum.h";
                              printf "/* Copyright 2019 Verisilicon(Beijing) Co.,Ltd. All Rights Reserved. */\n"> "8170table.h";
                              printf "/* Register interface based on the document version '$version' */\n">> "8170table.h";
#                              regNum=-1;
                              }
        END{printf "}; \n \n#endif /*SWREGISTERS_H_*/ \n">> "swregisters.h" }
        
        $1 ~ /^swreg/ {
                regNum=$1;
#                regNum=regNum+1;
                getline;
                for (i = 1; i <= NF; i++)
                {
                        if ($(i) ~ /Width/)
                                widthField=i;
                        else if ($(i) ~ /Name/)
                                nameField=i;
                        else if ($(i) ~ /Signed/)
                                signedField=i;
                        else if ($(i) ~ /Bit/)
                                bitField=i;
                        else if ($(i) ~ /trace/)
                        				trace_value_field=i;
                        else if ($(i) ~ /HEVC/)
                        				hevc_field=i;
                        else if ($(i) ~ /VP9/)
                        				vp9_field=i;
                        else if($(i) ~ /Function/)
                        				function_field=i;
                        else if($(i) ~ /Read\/Write/)
                        				RW_field=i;
                }	
                lastField=widthField>nameField ? widthField : nameField;
                lastField=bitField>lastField ? bitField : lastField;
                totalFields=NF;
        }
        $nameField ~ /sw_/  {
                # all required fields on current row
                numFields=NF;
                for (i = 0; i < numFields; i++)
                        rowArray[i] = $(i+1)
                fieldFinished = $0 ~ /;$/ ? 1 : 0;
                #for (i = 0; i < numFields; i++)
                #       printf "%d -> %s\n", i,rowArray[i]
                while (numFields < lastField)
                {
                        getline;
                        i = fieldFinished ? 0 : 1;
                        for (; i < NF; i++)
                                rowArray[numFields++] = $(i+1);
                        fieldFinished = $0 ~ /;$/ ? 1 : 0;
                }
                lsb=rowArray[bitField-1];
                reg=rowArray[nameField-1];
                modelreg=rowArray[nameField-1];
                width=rowArray[widthField-1];
                signed=rowArray[signedField-1];
                trace_value=rowArray[trace_value_field-1];
                function_value=rowArray[function_field-1];
                read_write_value=rowArray[RW_field-1];
                hevc_reg=rowArray[hevc_field-1];
                vp9_only_reg=rowArray[vp9_field-1];
  
                # certain flags had empty width field
                if (width == "")
                {
                        width=1;
                }
                # 15:10 -> 10 etc
                sub(/[0-9]*:/,"",lsb);
                sub(/swreg/,"",regNum);
                sub(/^sw_/,"hwif_",modelreg);
                ##decoder registers only
                msb=lsb+width-1;
                
                bit_occupy_high=0;
                bit_occupy_low=0;
                #printf "---lsb=%d,width=%d-----\n",lsb,width;
                for (k = 1; k <=int(width); k++)
                {
                	#printf "k=%d\n",k;
                	bit_occupy_high=bit_occupy_high*2;
                  if(bit_occupy_low>=32768)
                  {
                		bit_occupy_high=bit_occupy_high+1;
                	}
                	bit_occupy_low=bit_occupy_low*2;
                	if(bit_occupy_low>=65536)
                	{
                		bit_occupy_low=bit_occupy_low-65536;
                	}
									bit_occupy_low=bit_occupy_low+1;
									#printf "1bit_occupy_high=0x%0x\n",bit_occupy_high;
									#printf "1bit_occupy_low=0x%0x\n",bit_occupy_low;
								}
								#printf "-xxx--lsb=%d-----\n",lsb;
								for (j = 1; j <= int(lsb); j++)
                {
                	bit_occupy_high=bit_occupy_high*2;
                  if(bit_occupy_low>=32768)
                  {
                		bit_occupy_high=bit_occupy_high+1;
                	}
                	bit_occupy_low=bit_occupy_low*2;
                	if(bit_occupy_low>=65536)
                	{
                		bit_occupy_low=bit_occupy_low-65536;
                	}
                	
                	#printf "2bit_occupy_high=0x%0x\n",bit_occupy_high;
									#printf "2bit_occupy_low=0x%0x\n",bit_occupy_low;
								}

               if (msb > 31)
                {
                printf "error found in '$1' line \n"
                printf "%s definition over register limits: msb %d, lsb %d, width %d \n",reg,msb,lsb,width;
                }    
                 
                  regNum=int(regNum)   
                 
               #if((hevc_reg == "x"))  
               {
               if (regNum < 512)
                {
                  ##structure
                  if (signed == "x")
                  {
                  printf "  sai%d %s;\n",
                        width, reg >> "swregisters.h";
                  } else {
                  printf "  uai%d %s;\n",
                        width, reg >> "swregisters.h";
                  }

                  ##Directives
                  printf "directive set /$block/SwRegister.%s:rsc -MAP_TO_MODULE {[DirectInput]}\n",
                            reg >> "swregisters_directives.tcl";
                  ##HW stuff
                  if (width == 1)
                  {
                    printf " '$catapultPrefix'%s'$catapultPostfix'  <= swreg%d(%d);\n",
                          reg, regNum, msb >> "hwdriver.vhd";
                    printf " signal '$catapultPrefix'%s'$catapultPostfix' : std_logic;\n",
                          reg >> "signals.vhd";
                  }
                  else
                  {
                    printf " '$catapultPrefix'%s'$catapultPostfix'  <= swreg%d(%d downto %d);\n",
                          reg, regNum, msb, lsb >> "hwdriver.vhd";
                    printf " signal '$catapultPrefix'%s'$catapultPostfix' : std_logic_vector(%d downto 0);\n",
                          reg, width-1 >> "signals.vhd";
                  }
                }

                ##System model table
                # change widths of base addresses to 32 bits
                if (width == 30 && lsb == 2 && reg ~ /base$/)
                {
                    printf "    VCMDREG(%-40s, %-3d,0x%04x%04x, %8d, %8d,%2s,\"%-1s\"),\n",
                           toupper(modelreg), regNum*4, bit_occupy_high,bit_occupy_low, 0,trace_value,read_write_value,function_value >> "8170table.h";
                } else {
                    printf "    VCMDREG(%-40s, %-3d,0x%04x%04x, %8d, %8d,%2s,\"%-1s\"),\n",
                           toupper(modelreg), regNum*4,bit_occupy_high,bit_occupy_low, lsb,trace_value,read_write_value,function_value >> "8170table.h";
                }
                ##System model enumerations
                printf "    %s,\n", toupper(modelreg) >> "8170enum.h";
                }

        }
' "$fileName"

else
awk '
        BEGIN{FS=",";printf "//Copyright 2019 Verisilicon(Beijing) Co.,Ltd. All Rights Reserved.\n"> "swregisters.h";
                              printf "//Author: carl.li@verisilicon.com (Carl Li)\n">> "swregisters.h";
                              printf "//\n">> "swregisters.h";
                              printf "//Description: Common SWHW interface structure definition\n">> "swregisters.h";
                              printf "//Based on document version '$version'\n">> "swregisters.h";
                              printf "#ifndef SWREGISTERS_H_\n">> "swregisters.h";
                              printf "#define SWREGISTERS_H_\n\n">> "swregisters.h";
                              printf "#include \"actypes.h\"\n\n">> "swregisters.h";
                              printf "struct SwRegisters {\n">> "swregisters.h";
                              printf"--signal declaration from Document Version '$version'\n" > "signals.vhd";
                              printf"--register to signal map table from Document Version '$version'\n" > "hwdriver.vhd"
                              printf"##Common catapult directives from Document Version '$version' \n" > "swregisters_directives.tcl"
                              printf "/* Copyright 2019 Verisilicon(Beijing) Co.,Ltd. All Rights Reserved. */\n"> "8170enum.h";
                              printf "/* Register interface based on the document version '$version' */\n">> "8170enum.h";
                              printf "/* Copyright 2019 Verisilicon(Beijing) Co.,Ltd. All Rights Reserved. */\n"> "8170table.h";
                              printf "/* Register interface based on the document version '$version' */\n">> "8170table.h";
#                              regNum=-1;
                              }
        END{printf "}; \n \n#endif /*SWREGISTERS_H_*/ \n">> "swregisters.h" }
        
        $1 ~ /^swreg/ {
                regNum=$1;
                #regNum=regNum+1;
                getline;
                for (i = 1; i <= NF; i++)
                {
                        if ($(i) ~ /Width/)
                                widthField=i;
                        else if ($(i) ~ /Name/)
                                nameField=i;
                        else if ($(i) ~ /Signed/)
                                signedField=i;
                        else if ($(i) ~ /Bit/)
                                bitField=i;
                        else if ($(i) ~ /trace/)
                        				trace_value_field=i;
                        else if ($(i) ~ /HEVC/)
                        				hevc_field=i;
                        else if ($(i) ~ /VP9/)
                        				vp9_field=i;
                        else if($(i) ~ /Function/)
                        				function_field=i;
                        else if($(i) ~ /Read\/Write/)
                        				RW_field=i;
                }	
                lastField=widthField>nameField ? widthField : nameField;
                lastField=bitField>lastField ? bitField : lastField;
                totalFields=NF;
        }
        $nameField ~ /sw_/  {
                # all required fields on current row
                numFields=NF;
                for (i = 0; i < numFields; i++)
                        rowArray[i] = $(i+1)
                fieldFinished = $0 ~ /;$/ ? 1 : 0;
                #for (i = 0; i < numFields; i++)
                #       printf "%d -> %s\n", i,rowArray[i]
                while (numFields < lastField)
                {
                        getline;
                        i = fieldFinished ? 0 : 1;
                        for (; i < NF; i++)
                                rowArray[numFields++] = $(i+1);
                        fieldFinished = $0 ~ /;$/ ? 1 : 0;
                }
                lsb=rowArray[bitField-1];
                reg=rowArray[nameField-1];
                modelreg=rowArray[nameField-1];
                width=rowArray[widthField-1];
                signed=rowArray[signedField-1];
                trace_value=rowArray[trace_value_field-1];
                function_value=rowArray[function_field-1];
                read_write_value=rowArray[RW_field-1];
                hevc_reg=rowArray[hevc_field-1];
                vp9_only_reg=rowArray[vp9_field-1];
  
                # certain flags had empty width field
                if (width == "")
                {
                        width=1;
                }
                # 15:10 -> 10 etc
                sub(/[0-9]*:/,"",lsb);
                sub(/swreg/,"",regNum);
                sub(/^sw_/,"hwif_",modelreg);
                ##decoder registers only
                msb=lsb+width-1;
                
                bit_occupy_high=0;
                bit_occupy_low=0;
                #printf "---lsb=%d,width=%d-----\n",lsb,width;
                for (k = 1; k <=int(width); k++)
                {
                	#printf "k=%d\n",k;
                	bit_occupy_high=bit_occupy_high*2;
                  if(bit_occupy_low>=32768)
                  {
                		bit_occupy_high=bit_occupy_high+1;
                	}
                	bit_occupy_low=bit_occupy_low*2;
                	if(bit_occupy_low>=65536)
                	{
                		bit_occupy_low=bit_occupy_low-65536;
                	}
									bit_occupy_low=bit_occupy_low+1;
									#printf "1bit_occupy_high=0x%0x\n",bit_occupy_high;
									#printf "1bit_occupy_low=0x%0x\n",bit_occupy_low;
								}
								#printf "-xxx--lsb=%d-----\n",lsb;
								for (j = 1; j <= int(lsb); j++)
                {
                	bit_occupy_high=bit_occupy_high*2;
                  if(bit_occupy_low>=32768)
                  {
                		bit_occupy_high=bit_occupy_high+1;
                	}
                	bit_occupy_low=bit_occupy_low*2;
                	if(bit_occupy_low>=65536)
                	{
                		bit_occupy_low=bit_occupy_low-65536;
                	}
                	
                	#printf "2bit_occupy_high=0x%0x\n",bit_occupy_high;
									#printf "2bit_occupy_low=0x%0x\n",bit_occupy_low;
								}

               if (msb > 31)
                {
                printf "error found in '$1' line \n"
                printf "%s definition over register limits: msb %d, lsb %d, width %d \n",reg,msb,lsb,width;
                }    
                 
                  regNum=int(regNum)   
                 
               if((hevc_reg == "x")||((vp9_only_reg=="x")))  
               {
               if (regNum < 512)
                {
                  ##structure
                  if (signed == "x")
                  {
                  printf "  sai%d %s;\n",
                        width, reg >> "swregisters.h";
                  } else {
                  printf "  uai%d %s;\n",
                        width, reg >> "swregisters.h";
                  }

                  ##Directives
                  printf "directive set /$block/SwRegister.%s:rsc -MAP_TO_MODULE {[DirectInput]}\n",
                            reg >> "swregisters_directives.tcl";
                  ##HW stuff
                  if (width == 1)
                  {
                    printf " '$catapultPrefix'%s'$catapultPostfix'  <= swreg%d(%d);\n",
                          reg, regNum, msb >> "hwdriver.vhd";
                    printf " signal '$catapultPrefix'%s'$catapultPostfix' : std_logic;\n",
                          reg >> "signals.vhd";
                  }
                  else
                  {
                    printf " '$catapultPrefix'%s'$catapultPostfix'  <= swreg%d(%d downto %d);\n",
                          reg, regNum, msb, lsb >> "hwdriver.vhd";
                    printf " signal '$catapultPrefix'%s'$catapultPostfix' : std_logic_vector(%d downto 0);\n",
                          reg, width-1 >> "signals.vhd";
                  }
                }

                ##System model table
                # change widths of base addresses to 32 bits
                if (width == 30 && lsb == 2 && reg ~ /base$/)
                {
                    printf "    VCMDREG(%-40s, %-3d,0x%04x%04x, %8d, %8d,%2s,\"%-1s\"),\n",
                           toupper(modelreg), regNum*4, bit_occupy_high,bit_occupy_low, 0,trace_value,read_write_value,function_value >> "8170table.h";
                } else {
                    printf "    VCMDREG(%-40s, %-3d,0x%04x%04x, %8d, %8d,%2s,\"%-1s\"),\n",
                           toupper(modelreg), regNum*4,bit_occupy_high,bit_occupy_low, lsb,trace_value,read_write_value,function_value >> "8170table.h";
                }
                ##System model enumerations
                printf "    %s,\n", toupper(modelreg) >> "8170enum.h";
                }

        }
' "$fileName"

fi

cp 8170enum.h ../vcmdregisterenum.h
cp 8170table.h ../vcmdregistertable.h
