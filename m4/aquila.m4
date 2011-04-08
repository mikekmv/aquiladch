##################################################################################################
#
#
#                                     LOCAL PLUGIN MACRO
#
#
##################################################################################################

# AQ_PLUGIN_OPTION([PLUGIN_NAME], [option_name], [default])

AC_DEFUN([AQ_PLUGIN_OPTION],
[
	AC_ARG_ENABLE(
		[$2],
		AC_HELP_STRING([--enable-$2], [Turn on $2 plugin. (Default $3)]),
		[
			case "$enableval" in
				yes|true)
					aq_plugin_$2=true;
					;;
				no|false)
					aq_plugin_$2=false;
					;;
				*)
					AC_MSG_ERROR(bad value $enableval for --enable-$2)
					;;
			esac
		],
		[
			if test "$3" = "ON"
			then
				aq_plugin_$2=true;
			else
				unset aq_plugin_$2
			fi
		]
		)
	if test x$aq_plugin_$2 = xtrue
	then
		$1="-D$1"
	else	
		unset $1
	fi
	AM_CONDITIONAL([$1], test x$aq_plugin_$2 = xtrue)
	AC_SUBST([$1])
])

# AQ_PLUGIN_DISABLE([PLUGIN_NAME], [option_name])

AC_DEFUN([AQ_PLUGIN_DISABLE],
[
	unset aq_plugin_$2
	AM_CONDITIONAL([$1], false)
])

# AQ_PLUGIN_REPORT([option_name])
AC_DEFUN([AQ_PLUGIN_REPORT],
[
	l=`echo -n "   $1                                                                             " | cut -c 1-53`
	
	echo -n "$l"
	if test x$aq_plugin_$1 = xtrue
	then
		echo "ENABLED"
	else
		echo "DISABLED"
	fi
])

