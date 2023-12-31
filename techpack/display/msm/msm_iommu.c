/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "msm_drv.h"
#include "msm_mmu.h"

#ifdef OPLUS_BUG_STABILITY
#ifdef CONFIG_OPLUS_FEATURE_MM_FEEDBACK
#include <soc/oplus/system/oplus_mm_kevent_fb.h>
#endif /* CONFIG_OPLUS_FEATURE_MM_FEEDBACK */
#endif /* OPLUS_BUG_STABILITY */
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_THEIA)
#include <soc/oplus/system/theia_send_event.h>
#endif

struct msm_iommu {
	struct msm_mmu base;
	struct iommu_domain *domain;
};
#define to_msm_iommu(x) container_of(x, struct msm_iommu, base)

static int msm_fault_handler(struct iommu_domain *domain, struct device *dev,
		unsigned long iova, int flags, void *arg)
{
	struct msm_iommu *iommu = arg;
	if (iommu->base.handler)
		return iommu->base.handler(iommu->base.arg, iova, flags);
	pr_warn_ratelimited("*** fault: iova=%08lx, flags=%d\n", iova, flags);

#ifdef OPLUS_BUG_STABILITY
#ifdef CONFIG_OPLUS_FEATURE_MM_FEEDBACK
	mm_fb_display_kevent("DisplayDriverID@@404$$", MM_FB_KEY_RATELIMIT_1H, "SMMU msm fault iova=%08lx flags=%d", iova, flags);
#endif /* CONFIG_OPLUS_FEATURE_MM_FEEDBACK */
 #endif /*OPLUS_BUG_STABILITY*/
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_THEIA)
	theia_send_event(THEIA_EVENT_PTR_TIMEOUT_HANG, THEIA_LOGINFO_KERNEL_LOG, current->pid, "SMMU msm fault");
#endif

	return 0;
}

static int msm_iommu_attach(struct msm_mmu *mmu, const char * const *names,
			    int cnt)
{
	struct msm_iommu *iommu = to_msm_iommu(mmu);
	int ret;

	pm_runtime_get_sync(mmu->dev);
	ret = iommu_attach_device(iommu->domain, mmu->dev);
	pm_runtime_put_sync(mmu->dev);

	return ret;
}

static void msm_iommu_detach(struct msm_mmu *mmu, const char * const *names,
			     int cnt)
{
	struct msm_iommu *iommu = to_msm_iommu(mmu);

	pm_runtime_get_sync(mmu->dev);
	iommu_detach_device(iommu->domain, mmu->dev);
	pm_runtime_put_sync(mmu->dev);
}

static int msm_iommu_map(struct msm_mmu *mmu, uint64_t iova,
		struct sg_table *sgt, unsigned len, int prot)
{
	struct msm_iommu *iommu = to_msm_iommu(mmu);
	size_t ret;

//	pm_runtime_get_sync(mmu->dev);
	ret = iommu_map_sg(iommu->domain, iova, sgt->sgl, sgt->nents, prot);
//	pm_runtime_put_sync(mmu->dev);
	WARN_ON(!ret);

	return (ret == len) ? 0 : -EINVAL;
}

static int msm_iommu_unmap(struct msm_mmu *mmu, uint64_t iova,
		struct sg_table *sgt, unsigned len)
{
	struct msm_iommu *iommu = to_msm_iommu(mmu);

	pm_runtime_get_sync(mmu->dev);
	iommu_unmap(iommu->domain, iova, len);
	pm_runtime_put_sync(mmu->dev);

	return 0;
}

static void msm_iommu_destroy(struct msm_mmu *mmu)
{
	struct msm_iommu *iommu = to_msm_iommu(mmu);
	iommu_domain_free(iommu->domain);
	kfree(iommu);
}

static const struct msm_mmu_funcs funcs = {
		.attach = msm_iommu_attach,
		.detach = msm_iommu_detach,
		.map = msm_iommu_map,
		.unmap = msm_iommu_unmap,
		.destroy = msm_iommu_destroy,
};

struct msm_mmu *msm_iommu_new(struct device *dev, struct iommu_domain *domain)
{
	struct msm_iommu *iommu;

	iommu = kzalloc(sizeof(*iommu), GFP_KERNEL);
	if (!iommu)
		return ERR_PTR(-ENOMEM);

	iommu->domain = domain;
	msm_mmu_init(&iommu->base, dev, &funcs);
	iommu_set_fault_handler(domain, msm_fault_handler, iommu);

	return &iommu->base;
}
