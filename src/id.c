#define _XOPEN_SOURCE 700
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <grp.h>
#include <pwd.h>
#include <err.h>

int main(int argc, char *argv[])
{
	int opt_show_all_gid = 0;
	int opt_show_eff_gid = 0;
	int opt_show_name_str = 0;
	int opt_show_real_id = 0;
	int opt_show_eff_uid = 0;

	{
		int opt;
		while ((opt = getopt(argc, argv, "Ggnru")) != -1) {
			switch (opt) {
				case 'G':
					opt_show_all_gid = 1;
					break;
				case 'g':
					opt_show_eff_gid = 1;
					break;
				case 'n':
					opt_show_name_str = 1;
					break;
				case 'r':
					opt_show_real_id = 1;
					break;
				case 'u':
					opt_show_eff_uid = 1;
					break;
				default:
					fprintf(stderr, "Usage: %s -Ggu[nr] [user]\n", argv[0]);
					exit(EXIT_FAILURE);
			}
		}

		int only = opt_show_all_gid + opt_show_eff_gid + opt_show_eff_uid;

		if (only > 1) {
			errx(EXIT_FAILURE, "cannot print more than one only-view");
		}

		if (only == 0 && (opt_show_name_str + opt_show_real_id > 0)) {
			errx(EXIT_FAILURE, "cannot show names or IDs in normal format");
		}

		uid_t ruid, euid;
		gid_t rgid, egid;

		ruid = getuid();
		euid = geteuid();
		rgid = getgid();
		egid = getegid();

		struct passwd *ruid_pwd = getpwuid(ruid);
		struct passwd *euid_pwd = getpwuid(euid);
		struct group *rgid_pwd = getgrgid(rgid);
		struct group *egid_pwd = getgrgid(egid);

		if (opt_show_eff_gid) {
			if(!opt_show_real_id && opt_show_name_str)
				printf("%s\n", egid_pwd ? egid_pwd->gr_name : "");
			else if(opt_show_real_id && opt_show_name_str)
				printf("%s\n", rgid_pwd ? rgid_pwd->gr_name : "");
			else if(opt_show_real_id)
				printf("%d\n", rgid);
			else
				printf("%d\n", egid);
			exit(EXIT_SUCCESS);
		}

		if (opt_show_all_gid) {
			printf("Not implemented\n");
			exit(EXIT_FAILURE);
		}

		printf("uid=%u(%s)",
				ruid, 
				ruid_pwd ? ruid_pwd->pw_name : "");

		if (euid != ruid)
			printf(" euid=%u(%s)",
					euid,
					euid_pwd ? euid_pwd->pw_name : "");

		printf(" gid=%u(%s)", 
				rgid,
				rgid_pwd ? rgid_pwd->gr_name : "");

		if (egid != rgid)
			printf(" egid=%u(%s)",
					egid,
					egid_pwd ? egid_pwd->gr_name : "");

		long ngroups_max = sysconf(_SC_NGROUPS_MAX);
		if (ngroups_max>0) {

			gid_t *list = calloc(ngroups_max, sizeof(gid_t));
			if (list == NULL)
				err(EXIT_FAILURE, NULL);
			
			int num;
			if ((num = getgroups(ngroups_max, list)) == -1)
				err(EXIT_FAILURE, NULL);

			for (int i=0;i<num;i++) {
				struct group *supg_pwd = getgrgid(list[i]);
				if (i==0)
					printf(" groups=%u(%s)", list[i], 
							supg_pwd ? supg_pwd->gr_name : "");
				else
					printf(",%u(%s)", list[i],
							supg_pwd ? supg_pwd->gr_name : "");
			}
		}

		putchar('\n');

	}


}
