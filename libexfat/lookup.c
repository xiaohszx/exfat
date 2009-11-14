/*
 *  lookup.c
 *  exFAT file system implementation library.
 *
 *  Created by Andrew Nayenko on 02.09.09.
 *  This software is distributed under the GNU General Public License 
 *  version 3 or any later.
 */

#include "exfat.h"
#include <string.h>
#include <errno.h>
#include <inttypes.h>

int exfat_opendir(struct exfat* ef, struct exfat_node* dir,
		struct exfat_iterator* it)
{
	int rc;

	exfat_get_node(dir);
	it->parent = dir;
	it->current = NULL;
	rc = exfat_cache_directory(ef, dir);
	if (rc != 0)
		exfat_put_node(ef, dir);
	return rc;
}

void exfat_closedir(struct exfat* ef, struct exfat_iterator* it)
{
	exfat_put_node(ef, it->parent);
	it->parent = NULL;
	it->current = NULL;
}

struct exfat_node* exfat_readdir(struct exfat* ef, struct exfat_iterator* it)
{
	if (it->current == NULL)
		it->current = it->parent->child;
	else
		it->current = it->current->next;

	if (it->current != NULL)
		return exfat_get_node(it->current);
	else
		return NULL;
}

static int compare_char(struct exfat* ef, uint16_t a, uint16_t b)
{
	if (a >= ef->upcase_chars || b >= ef->upcase_chars)
		return (int) a - (int) b;

	return (int) le16_to_cpu(ef->upcase[a]) - (int) le16_to_cpu(ef->upcase[b]);
}

static int compare_name(struct exfat* ef, const le16_t* a, const le16_t* b)
{
	while (le16_to_cpu(*a) && le16_to_cpu(*b))
	{
		int rc = compare_char(ef, le16_to_cpu(*a), le16_to_cpu(*b));
		if (rc != 0)
			return rc;
		a++;
		b++;
	}
	return compare_char(ef, le16_to_cpu(*a), le16_to_cpu(*b));
}

static int lookup_name(struct exfat* ef, struct exfat_node* parent,
		struct exfat_node** node, const char* name, size_t n)
{
	struct exfat_iterator it;
	le16_t buffer[EXFAT_NAME_MAX + 1];
	int rc;

	rc = utf8_to_utf16(buffer, name, EXFAT_NAME_MAX, n);
	if (rc != 0)
		return rc;

	rc = exfat_opendir(ef, parent, &it);
	if (rc != 0)
		return rc;
	while ((*node = exfat_readdir(ef, &it)))
	{
		if (compare_name(ef, buffer, (*node)->name) == 0)
		{
			exfat_closedir(ef, &it);
			return 0;
		}
		exfat_put_node(ef, *node);
	}
	exfat_closedir(ef, &it);
	return -ENOENT;
}

static size_t get_comp(const char* path, const char** comp)
{
	const char* end;

	*comp = path + strspn(path, "/");				/* skip leading slashes */
	end = strchr(*comp, '/');
	if (end == NULL)
		return strlen(*comp);
	else
		return end - *comp;
}

int exfat_lookup(struct exfat* ef, struct exfat_node** node,
		const char* path)
{
	struct exfat_node* parent;
	const char* p;
	size_t n;

	/* start from the root directory */
	parent = *node = exfat_get_node(ef->root);
	for (p = path; (n = get_comp(p, &p)); p += n)
	{
		if (n == 1 && *p == '.')				/* skip "." component */
			continue;
		if (lookup_name(ef, parent, node, p, n) != 0)
		{
			exfat_put_node(ef, parent);
			return -ENOENT;
		}
		exfat_put_node(ef, parent);
		parent = *node;
	}
	return 0;
}
