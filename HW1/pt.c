#include "os.h"

/*
Explanation - 5 levels in the trie because there are 512 rows in a page table node 
(512 = (size_of_page_table)/(size_of_pte)), we need 9 bits to search in 512 rows table and 
there are 45 representing the VPN so we get 5 levels.
The page walk in both functions is done in a 5 iterations for loop, at each iteration taking the top 9 bits of
the vpn as the offset for the ith level pt, and shift 9 again for the next level. 
We start by shifting the vpn 19 bits to the left to glue the 45 relevant bits to the left
(ignore top 7, and 12 more to put 45 bits in the leftmost places). 
*/
void page_table_update( uint64_t pt,  uint64_t vpn,  uint64_t ppn)
{
	uint64_t* level_pt; 
	uint64_t offset, temp_vpn;
	int i;

	/*shit left 12 in order to set 12 lowest bits to 0*/
	level_pt = phys_to_virt(pt << 12);
	temp_vpn = vpn;
	/*ignore top 7 bits and glue the rest to the left*/
	temp_vpn = temp_vpn << 19;
	for(i = 0; i < 5; i++)
	{
		/*take top 9 bits*/
		offset = ((1 << 9) - 1) & (temp_vpn >> (56 - 1));
		if(i == 4)
		{
			/*condition on last iteration to prevent another phys_to_virt*/
			if(ppn == NO_MAPPING)
			{
				/*setting the pte to 0 will set valid bit to 0, means no mapping*/
				level_pt[offset] = 0;
			}
			else
			{
				/*set pte 12 lowest bits to 0, add 1 to turn on valid bit*/
				level_pt[offset] = (ppn << 12)+1;
			}
			return;
		}
		/*if there is no mapping there (i.e valid bit off)*/
		if(((level_pt[offset]) & 1) == 0)
		{
			if(ppn == NO_MAPPING)
			{
				return;
			}
			else
			{
				/*set pte 12 lowest bits to 0, add 1 to turn on valid bit*/
				level_pt[offset] = (alloc_page_frame() << 12) + 1;
			}
		}
		/*-1 to turn off valid bit because phys_to_virt assumes 12 lowest bits are 0 
		(all 11 but valid bit are already 0 since we shifted left when recived)*/
		level_pt = phys_to_virt(((level_pt[offset])-1));
		/*make 9 top bits the relevant for next level*/
		temp_vpn = temp_vpn << 9;
	}
}
uint64_t page_table_query(uint64_t pt, uint64_t vpn)
{
	 uint64_t* level_pt; 
	 uint64_t offset, temp_vpn;
	int i;

	level_pt = phys_to_virt(pt << 12);
	temp_vpn = vpn;
	temp_vpn = temp_vpn << 19;
	for(i = 0; i < 5; i++)
	{
		offset = ((1 << 9) - 1) & (temp_vpn >> (56 - 1));
		/*valid bit off meaning there is no mapping*/
		if(((level_pt[offset]) & 1) == 0)
		{
			return NO_MAPPING;
		}
		if(i == 4)
		{
			/*shift right 12 to return 52 bits that represent the ppn,
			in the page table 12 rightmost bits are 0 but the valid bit*/
	        return (level_pt[offset] >> 12);
		}
		level_pt = phys_to_virt(((level_pt[offset])-1));
		temp_vpn = temp_vpn << 9;
	}
	return 0;
}