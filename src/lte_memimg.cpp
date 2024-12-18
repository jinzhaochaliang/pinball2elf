/*BEGIN_LEGAL 
BSD License 

Copyright (c)2019 Intel Corporation. All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
#include "lte_memimg.h"
#include "lte_string.h"
#include "lte_portability.h"

#include <stdio.h>
#include <algorithm>
#include <vector>
#include <iostream>
#include <iomanip>

#define print_hex(v, w) std::hex<<std::setfill('0')<<std::setw(w)<<(v)<<std::setfill(' ')<<std::dec
#define print_dec(v, w) std::dec<<std::setfill(' ')<<std::setw(w)<<(v)<<std::setfill(' ')<<std::dec
#define print_shf(shf) \
      (((shf)&SHF_EXECINSTR)?'x':'-')<<(((shf)&SHF_WRITE)?'w':'-')<<(((shf)&SHF_ALLOC)?'a':'-')<<\
      (((shf)&SHF_ENTRYPOINT)?'e':'-')<<(((shf)&SHF_DYNALLOC)?'d':'-')


#define SHF_TYPE_MASK   (SHF_WRITE|SHF_EXECINSTR|SHF_ALLOC)

lte_mempage_t::lte_mempage_t()
{
   region_next = NULL;
   region_size = LTE_PAGE_SIZE;
   va = 0;
   pa = 0;
   offs_start = LTE_PAGE_SIZE;
   offs_end = 0;
   memset(data, 0x00, sizeof(data));
}

lte_uint32_t lte_mempage_t::set(lte_uint32_t offs, const lte_uint8_t* p, lte_uint32_t size)
{
   lte_uint32_t size_not_copied = (offs + size > sizeof(data)) ? (sizeof(data) - offs) : 0;

   memcpy(data + offs, p, size -= size_not_copied);

   if(offs_start > offs)
      offs_start = offs;

   offs += size;

   if(offs_end < offs)
      offs_end = offs;

   return size_not_copied;
}

static bool pagecmp(lte_mempage_t* p1, lte_mempage_t* p2)
{
   lte_size64_t delta1, delta2;

   if(p1->region_next != NULL)
   {
      delta1 = p1->region_next->va - (p1->va + p1->region_size);
   }
   else
   {
      // p1 > p2
      return false;
   }

   if(p2->region_next != NULL)
   {
      delta2 = p2->region_next->va - (p2->va + p2->region_size);
   }
   else
   {
      // p1 <= p2
      return true;
   }

   return (delta1 < delta2);
}

static __LTE_INLINE bool adjacent_pages(lte_mempage_t& pg1, lte_mempage_t& pg2) 
{
   return (pg1.va - pg2.va == LTE_PAGE_SIZE);
}

static __LTE_INLINE bool same_type_pages(lte_mempage_t& pg1, lte_mempage_t& pg2, Elf64_Xword flags) 
{
   return !((pg2.flags ^ pg1.flags) & flags);
}

static __LTE_INLINE bool can_combine_pages(lte_mempage_t& pg1, lte_mempage_t& pg2, Elf64_Xword flags) 
{
   return adjacent_pages(pg1, pg2) && same_type_pages(pg1, pg2, flags);
}

void lte_memimg_t::add_missing_page(lte_addr_t missing_page_addr, lte_uint8_t *page_content)
{
  lte_uint32_t missing_page_offs = 0;
  lte_mempage_t* missing_page = NULL;
  missing_page_offs = missing_page_addr & (lte_addr_t)(page_size-1);
  LTE_ERRAX(missing_page_offs, "Non-zero page_offset 0x%x for missing_page_addr 0x%x", missing_page_offs, missing_page_addr);
  missing_page_addr &= ~(lte_addr_t)(page_size-1);
  
  missing_page = &m_mem[missing_page_addr];
  // std::cout << " Adding missing page " << std::hex << missing_page_addr << std::endl;
  missing_page->va = missing_page_addr;
  missing_page->flags = (SHF_WRITE|SHF_EXECINSTR|SHF_ALLOC);
  lte_uint32_t rest = missing_page->set(missing_page_offs, page_content, page_size);
  LTE_ERRAX(rest, "Error copying text page at 0x%x", missing_page_addr);
}

lte_uint64_t lte_memimg_t::compact(lte_uint64_t regions_max)
{
   if(m_mem.size() < 2)
      return 0;

   std::vector<lte_mempage_t*> pages;
   std::map<lte_addr_t, lte_mempage_t>::iterator pg = m_mem.begin();
   lte_uint64_t regions_count = 1;
   lte_uint64_t offs = 0;
   lte_uint8_t zero_page_content[page_size];
   memset(zero_page_content, 0, page_size);

   // Discovered with gcc whole-program pinball:
   // address file has a lot of 1-page holes e.g.:
   //        0x2aaac4e43000 0x2aaac4e4a000 data 0
   //        0x2aaac4e4b000 0x2aaac4e97000 text 0
   // The loop below discovers such holes and plugs them by adding
   //  the missing page. This causes all mmap() offsets in the generated
   //   ELFie to be contiguous. Without that, the mmap() offsets were drifting
   //    away causing wrong memory to be restored.
   for(std::map<lte_addr_t, lte_mempage_t>::iterator pg_prev = pg++; pg != m_mem.end(); ++pg)
   {
      if(pg->first == pg_prev->first+0x2000)
      {
        //std::cout << " missing page between 0x" << std::hex <<
        //    pg_prev->first << " and 0x" << std::hex << pg->first << std::endl;
        {
          add_missing_page(pg_prev->first+0x1000, &zero_page_content[0]);
        }
      }
#if 0
      if(pg->first == pg_prev->first+0x3000)
      {
        std::cout << " 2 missing pages between 0x" << std::hex <<
            pg_prev->first << " and 0x" << std::hex << pg->first << std::endl;
        {
          add_missing_page(pg_prev->first+0x1000, &zero_page_content[0]);
          add_missing_page(pg_prev->first+0x2000, &zero_page_content[0]);
        }
      }
      if(pg->first == pg_prev->first+0x4000)
      {
        std::cout << " 3 missing pages between 0x" << std::hex <<
            pg_prev->first << " and 0x" << std::hex << pg->first << std::endl;
        {
          add_missing_page(pg_prev->first+0x1000, &zero_page_content[0]);
          add_missing_page(pg_prev->first+0x2000, &zero_page_content[0]);
          add_missing_page(pg_prev->first+0x3000, &zero_page_content[0]);
        }
      }
#endif
      pg_prev = pg;
   }

   pg = m_mem.begin();
   lte_mempage_t* head = &pg->second;
   pg->second.region_size = LTE_PAGE_SIZE;
   pg->second.region_next = NULL;
   pg->second.next = NULL;
   pg->second.head = head;

   for(std::map<lte_addr_t, lte_mempage_t>::iterator pg_prev = pg++; pg != m_mem.end(); ++pg)
   {
      pg->second.region_size = LTE_PAGE_SIZE;
      pg->second.region_next = NULL;
      pg->second.next = NULL;

      if(can_combine_pages(pg->second, pg_prev->second, SHF_TYPE_MASK|SHF_ENTRYPOINT|SHF_DYNALLOC))
      {
         pg_prev->second.next = &pg->second;

         offs += LTE_PAGE_SIZE;
         pg->second.offs = offs;
         head->region_size += LTE_PAGE_SIZE;
      }
      else
      {
         if(regions_max)
            pages.push_back(head);
         ++regions_count;

         head->region_next = &pg->second;
         head = &pg->second;
         pg->second.offs = offs = 0;
      }
      pg->second.head = head;
      pg_prev = pg;
   }

   if(offs)
   {
      head->region_next = NULL;
      // don't push last region into pages
      // we want only pages with region_next not equal to NULL
   }

   if(regions_max && regions_count > regions_max)
   {
      sort(pages.begin(), pages.end(), pagecmp);

      for(std::vector<lte_mempage_t*>::iterator it = pages.begin(); it != pages.end(); ++it)
      {
         lte_mempage_t* p = *it;
         if(same_type_pages(*p, *p->region_next, SHF_EXECINSTR|SHF_ALLOC|SHF_ENTRYPOINT|SHF_DYNALLOC))
         {
            if (p->region_next->va - p->va > 0x40000000)
            {
               continue;
            }
            p->head = NULL;
            if(--regions_count < regions_max)
               break;
         }
      }

      for(head = &m_mem.begin()->second; head; head = head->region_next)
      {
         if(!head->head)
         {
            lte_mempage_t* region = head;

            while(!region->head)
            {
               lte_mempage_t* p = (lte_mempage_t*)region->get_last();
               region = region->region_next;
               p->next = region;
            }

            lte_mempage_t* region_next = region->region_next;
            Elf64_Xword offs = 0;
            for(lte_mempage_t* p = head; p; p = (lte_mempage_t*)p->next)
            {
               p->offs = offs = p->va - head->va;
               p->head = head;
               p->region_next = NULL;
               p->region_size = LTE_PAGE_SIZE;
            }
            head->region_next = region_next;
            head->region_size = offs + LTE_PAGE_SIZE;
         }
      }
   }

   return regions_count;
}

lte_mempage_t* lte_memimg_t::get_page(lte_addr_t addr)
{
   addr &=~(lte_addr_t)(LTE_PAGE_SIZE-1);
   return m_mem.count(addr) ? &m_mem[addr] : NULL;
}

lte_addr_t lte_memimg_t::find_free_block(lte_uint64_t size, lte_uint32_t shflags)
{
   if(m_mem.size())
   {
      std::map<lte_addr_t, lte_mempage_t>::iterator it = m_mem.begin();
      lte_addr_t addr = it->first + LTE_PAGE_SIZE;
      lte_addr_t addr_end = addr;

      for (++it; it != m_mem.end(); ++it)
      {
         addr_end = it->first;

         if(addr_end - addr >= size)
            return addr;

         addr = addr_end + LTE_PAGE_SIZE;
      }
      return addr;
   }
   return 0;
}

lte_addr_t lte_memimg_t::insert(const lte_uint8_t* p, lte_uint64_t size, lte_uint32_t shflags)
{
   lte_addr_t addr_start = find_free_block(size, shflags);
   lte_addr_t addr_end = addr_start + size;
   lte_addr_t addr_last_pg = addr_end & ~(lte_addr_t)(LTE_PAGE_SIZE-1);

   if(!addr_start)
      return 0;

   lte_addr_t addr = addr_start;

   for(; addr < addr_last_pg; addr += LTE_PAGE_SIZE)
   {
      lte_mempage_t& page = m_mem[addr];
      if(p)
      {
         page.set(0, p, LTE_PAGE_SIZE);
         p += LTE_PAGE_SIZE;
      }
      page.flags = shflags;
      page.va = addr;
   }

   if(addr_last_pg < addr_end)
   {
      lte_mempage_t& page = m_mem[addr];
      if(p)
      {
         page.set(0, p, addr_end - addr_last_pg);
      }
      page.flags = shflags;
      page.va = addr;
   }
   return addr_start;
}

lte_uint64_t lte_memimg_t::memcopy(lte_addr_t addr, const lte_uint8_t* p, lte_uint64_t size)
{
   if(!size || !p)
      return 0;

   lte_addr_t addr_start = addr;
   lte_addr_t addr_end = addr_start + size;
   lte_addr_t addr_page = addr_start & ~(lte_addr_t)(LTE_PAGE_SIZE-1);
   lte_addr_t addr_page_last = addr_end & ~(lte_addr_t)(LTE_PAGE_SIZE-1);

   if(addr_page < addr_start)
   {
      if(!m_mem.count(addr_page))
         return 0;

      lte_uint64_t offs = addr_start & (lte_addr_t)(LTE_PAGE_SIZE-1);

      if(offs + size < LTE_PAGE_SIZE)
      {
         m_mem[addr_page].set(addr_start - addr_page, p, size);
         return size;
      }

      m_mem[addr_page].set(addr_start - addr_page, p, LTE_PAGE_SIZE - offs);
      p += LTE_PAGE_SIZE - offs;
   }

   for(; addr_page < addr_page_last; addr_page += LTE_PAGE_SIZE, p += LTE_PAGE_SIZE)
   {
      if(!m_mem.count(addr_page))
         return addr_page - addr_start;
      m_mem[addr_page].set(0, p, LTE_PAGE_SIZE);
   }

   if(addr_page < addr_end)
   {
      if(!m_mem.count(addr_page))
         return addr_page - addr_start;

      m_mem[addr_page].set(0, p, addr_end - addr_page);
   }

   return size;
}

void lte_memimg_t::mark(lte_addr_t vastart, lte_addr_t vaend, lte_uint32_t shflags)
{
   if(vastart == vaend)
      ++vaend;

   for(vastart &= ~(lte_addr_t)(LTE_PAGE_SIZE-1); vastart < vaend; vastart += LTE_PAGE_SIZE)
   {
      if(m_mem.count(vastart))
      {
         lte_mempage_t& page = m_mem[vastart];
         page.flags = (page.flags & ~SHF_TYPE_MASK) | (shflags & SHF_TYPE_MASK);
      }
   }
}

lte_uint64_t lte_memimg_t::get_regions_number(lte_uint64_t flags)
{
   lte_uint64_t num = 0;

   for(lte_mempage_t* r = &m_mem.begin()->second; r; r = r->region_next)
   {
      if(r->flags & flags)
         ++num;
   }
   return num;
}

void lte_memimg_t::print()
{
   std::cout<<"--------------------------- MEMORY IMAGE ---------------------------\n";
   for(lte_mempage_t* r = &m_mem.begin()->second; r; r = r->region_next)
   {
      std::cout<<"--->"<<print_hex(r->va,16)<<" ["<<print_hex(r->offs,6)<<"]: "<<print_shf(r->flags)<<' '<<print_dec(r->region_size,8)<<'\n';
      for(lte_mempage_t* p = (lte_mempage_t*)r->next; p; p = (lte_mempage_t*)p->next)
      {
         std::cout<<"   +"<<print_hex(p->va,16)<<" ["<<print_hex(p->offs,6)<<"]: "<<print_shf(p->flags)<<' '<<print_dec(p->region_size,8)<<'\n';
      }
   }
   std::cout<<"--------------------------------------------------------------------\n";
}

lte_addr_t lte_mem_layout_t::insert(lte_addr_t va, lte_uint64_t size)
{
   lte_addr_t vastart = __lte_align(va, m_block_size);

   if(!size)
      return LTE_MAXVAL(lte_addr_t);

   if(!va)
   {
      vastart = find_free_block(size);
      if(!vastart)
         return LTE_MAXVAL(lte_addr_t);

      region_t& r = m_regions[vastart];
      r.vastart = vastart;
      r.vaend = __lte_align_up(va + size, m_block_size);
   }
   else
   {
      lte_addr_t vaend = __lte_align_up(va + size, m_block_size);

      std::pair<iterator,bool> ret = m_regions.insert( std::pair<lte_addr_t,region_t>(vastart,region_t()) );

      iterator it = ret.first;
      for(++it; it != m_regions.end(); m_regions.erase(it++))
      {
         if(it->second.vastart > vaend)
            break;
         if(it->second.vaend > vaend)
            vaend = it->second.vaend;
      }

      if(ret.second)
      {
         ret.first->second.vastart = vastart;
         ret.first->second.vaend = vaend;

         if(ret.first != m_regions.begin())
         {
            iterator it = ret.first;
            --it;

            if(it->second.vaend >= vastart)
            {
               if(it->second.vaend < vaend)
                  it->second.vaend = vaend;
               m_regions.erase(ret.first);
            }
         }
      }
      else
      {
         if(ret.first->second.vaend < vaend)
            ret.first->second.vaend = vaend;
      } 
   }

   return vastart;
}

lte_addr_t lte_mem_layout_t::insert(lte_uint64_t size)
{
   lte_addr_t va = find_free_block(size);
   return va ? insert(va, size) : va;
}

lte_addr_t lte_mem_layout_t::find_free_block(lte_uint64_t size)
{
   if(m_regions.size())
   {
      size = __lte_align_up(size, m_block_size);

      std::map<lte_addr_t, region_t>::iterator it = m_regions.begin();
      lte_addr_t va = it->second.vaend;
      for (++it; it != m_regions.end(); ++it)
      {
         if(it->second.vastart - va >= size)
            return va;
         va = it->second.vaend;
      }
      return va;
   }
   return 0;
}


void lte_mem_layout_t::print()
{
   for(std::map<lte_addr_t, region_t>::iterator it = m_regions.begin(); it != m_regions.end(); ++it)
   {
      region_t& rgn = it->second;
      std::cout << print_hex(rgn.vastart, 12) << " - " << print_hex(rgn.vaend, 12) << '\n';
   }
}
